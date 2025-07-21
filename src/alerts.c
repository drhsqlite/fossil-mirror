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
** Logic for email notification, also known as "alerts" or "subscriptions".
**
** Are you looking for the code that reads and writes the internet
** email protocol?  That is not here.  See the "smtp.c" file instead.
** Yes, the choice of source code filenames is not the greatest, but
** it is not so bad that changing them seems justified.
*/
#include "config.h"
#include "alerts.h"
#include <assert.h>
#include <time.h>

/*
** Maximum size of the subscriberCode blob, in bytes
*/
#define SUBSCRIBER_CODE_SZ 32

/*
** SQL code to implement the tables needed by the email notification
** system.
*/
static const char zAlertInit[] =
@ DROP TABLE IF EXISTS repository.subscriber;
@ -- Subscribers are distinct from users.  A person can have a log-in in
@ -- the USER table without being a subscriber.  Or a person can be a
@ -- subscriber without having a USER table entry.  Or they can have both.
@ -- In the last case the suname column points from the subscriber entry
@ -- to the USER entry.
@ --
@ -- The ssub field is a string where each character indicates a particular
@ -- type of event to subscribe to.  Choices:
@ --     a - Announcements
@ --     c - Check-ins
@ --     f - Forum posts
@ --     k - ** Special: Unsubscribed using /oneclickunsub
@ --     n - New forum threads
@ --     r - Replies to my own forum posts
@ --     t - Ticket changes
@ --     u - Changes of users' permissions (admins only)
@ --     w - Wiki changes
@ --     x - Edits to forum posts
@ -- Probably different codes will be added in the future.  In the future
@ -- we might also add a separate table that allows subscribing to email
@ -- notifications for specific branches or tags or tickets.
@ --
@ CREATE TABLE repository.subscriber(
@   subscriberId INTEGER PRIMARY KEY, -- numeric subscriber ID.  Internal use
@   subscriberCode BLOB DEFAULT (randomblob(32)) UNIQUE, -- UUID for subscriber
@   semail TEXT UNIQUE COLLATE nocase,-- email address
@   suname TEXT,                      -- corresponding USER entry
@   sverified BOOLEAN DEFAULT true,   -- email address verified
@   sdonotcall BOOLEAN,               -- true for Do Not Call
@   sdigest BOOLEAN,                  -- true for daily digests only
@   ssub TEXT,                        -- baseline subscriptions
@   sctime INTDATE,                   -- When this entry was created. unixtime
@   mtime INTDATE,                    -- Last change.  unixtime
@   smip TEXT,                        -- IP address of last change
@   lastContact INT                   -- Last contact. days since 1970
@ );
@ CREATE INDEX repository.subscriberUname
@   ON subscriber(suname) WHERE suname IS NOT NULL;
@
@ DROP TABLE IF EXISTS repository.pending_alert;
@ -- Email notifications that need to be sent.
@ --
@ -- The first character of the eventid determines the event type.
@ -- Remaining characters determine the specific event.  For example,
@ -- 'c4413' means check-in with rid=4413.
@ --
@ CREATE TABLE repository.pending_alert(
@   eventid TEXT PRIMARY KEY,         -- Object that changed
@   sentSep BOOLEAN DEFAULT false,    -- individual alert sent
@   sentDigest BOOLEAN DEFAULT false, -- digest alert sent
@   sentMod BOOLEAN DEFAULT false     -- pending moderation alert sent
@ ) WITHOUT ROWID;
@
@ -- Obsolete table.  No longer used.
@ DROP TABLE IF EXISTS repository.alert_bounce;
;

/*
** Return true if the email notification tables exist.
*/
int alert_tables_exist(void){
  return db_table_exists("repository", "subscriber");
}

/*
** Record the fact that user zUser has made contact with the repository.
** This resets the subscription timeout on that user.
*/
void alert_user_contact(const char *zUser){
  if( db_table_has_column("repository","subscriber","lastContact") ){
    db_unprotect(PROTECT_READONLY);
    db_multi_exec(
      "UPDATE subscriber SET lastContact=now()/86400 WHERE suname=%Q",
      zUser
    );
    db_protect_pop();
  }
}

/*
** Make sure the table needed for email notification exist in the repository.
**
** If the bOnlyIfEnabled option is true, then tables are only created
** if the email-send-method is something other than "off".
*/
void alert_schema(int bOnlyIfEnabled){
  if( !alert_tables_exist() ){
    if( bOnlyIfEnabled
     && fossil_strcmp(db_get("email-send-method",0),"off")==0
    ){
      return;  /* Don't create table for disabled email */
    }
    db_exec_sql(zAlertInit);
    return;
  }
  if( db_table_has_column("repository","subscriber","lastContact") ){
    return;
  }
  db_unprotect(PROTECT_READONLY);
  db_multi_exec(
    "DROP TABLE IF EXISTS repository.alert_bounce;\n"
    "ALTER TABLE repository.subscriber ADD COLUMN lastContact INT;\n"
    "UPDATE subscriber SET lastContact=mtime/86400;"
  );
  db_protect_pop();
  if( db_table_has_column("repository","pending_alert","sentMod") ){
    return;
  }
  db_multi_exec(
    "ALTER TABLE repository.pending_alert"
    " ADD COLUMN sentMod BOOLEAN DEFAULT false;"
  );
}

/*
** Process deferred alert events.  Return the number of errors.
*/
static int alert_process_deferred_triggers(void){
  if( db_table_exists("temp","deferred_chat_events")
   && db_table_exists("repository","chat")
  ){
    const char *zChatUser = db_get("chat-timeline-user", 0);
    if( zChatUser && zChatUser[0] ){
      db_multi_exec(
        "INSERT INTO chat(mtime,lmtime,xfrom,xmsg)"
        " SELECT julianday(), "
               " strftime('%%Y-%%m-%%dT%%H:%%M:%%S','now','localtime'),"
               " %Q,"
               " chat_msg_from_event(type, objid, user, comment)\n"
        "   FROM deferred_chat_events;\n",
        zChatUser
      );
    }
  }
  return 0;
}

/*
** Enable triggers that automatically populate the pending_alert
** table. (Later:) Also add triggers that automatically relay timeline
** events to chat, if chat is configured for that.
*/
void alert_create_trigger(void){
  if( db_table_exists("repository","pending_alert") ){
    db_multi_exec(
      "DROP TRIGGER IF EXISTS repository.alert_trigger1;\n" /* Purge legacy */
      "CREATE TRIGGER temp.alert_trigger1\n"
      "AFTER INSERT ON repository.event BEGIN\n"
      "  INSERT INTO pending_alert(eventid)\n"
      "    SELECT printf('%%.1c%%d',new.type,new.objid) WHERE true\n"
      "    ON CONFLICT(eventId) DO NOTHING;\n"
      "END;"
    );
  }
  if( db_table_exists("repository","chat")
   && db_get("chat-timeline-user", "")[0]!=0
  ){
    /* Record events that will be relayed to chat, but do not relay
    ** them immediately, as the chat_msg_from_event() function requires
    ** that TAGXREF be up-to-date, and that has not happened yet when
    ** the insert into the EVENT table occurs.  Make arrangements to
    ** invoke alert_process_deferred_triggers() when the transaction
    ** commits.  The TAGXREF table will be ready by then. */
    db_multi_exec(
       "CREATE TABLE temp.deferred_chat_events(\n"
       "  type TEXT,\n"
       "  objid INT,\n"
       "  user TEXT,\n"
       "  comment TEXT\n"
       ");\n"
       "CREATE TRIGGER temp.chat_trigger1\n"
       "AFTER INSERT ON repository.event BEGIN\n"
       "  INSERT INTO deferred_chat_events"
       "   VALUES(new.type,new.objid,new.user,new.comment);\n"
       "END;\n"
    );
    db_commit_hook(alert_process_deferred_triggers, 1);
  }
}

/*
** Disable triggers the event_pending and chat triggers.
**
** This must be called before rebuilding the EVENT table, for example
** via the "fossil rebuild" command.
*/
void alert_drop_trigger(void){
  db_multi_exec(
    "DROP TRIGGER IF EXISTS temp.alert_trigger1;\n"
    "DROP TRIGGER IF EXISTS repository.alert_trigger1;\n" /* Purge legacy */
    "DROP TRIGGER IF EXISTS temp.chat_trigger1;\n"
  );
}

/*
** Return true if email alerts are active.
*/
int alert_enabled(void){
  if( !alert_tables_exist() ) return 0;
  if( fossil_strcmp(db_get("email-send-method",0),"off")==0 ) return 0;
  return 1;
}

/*
** If alerts are enabled, removes the pending_alert entry which
** matches (eventType || rid). Note that pending_alert entries are
** added via the manifest crosslinking process, so this has no effect
** if called before crosslinking is performed. Because alerts are sent
** asynchronously, unqueuing needs to be performed as part of the
** transaction in which crosslinking is performed in order to avoid a
** race condition.
*/
void alert_unqueue(char eventType, int rid){
  if( alert_enabled() ){
    db_multi_exec("DELETE FROM pending_alert WHERE eventid='%c%d'",
                  eventType, rid);
  }
}

/*
** If the subscriber table does not exist, then paint an error message
** web page and return true.
**
** If the subscriber table does exist, return 0 without doing anything.
*/
static int alert_webpages_disabled(void){
  if( alert_tables_exist() ) return 0;
  style_set_current_feature("alerts");
  style_header("Email Alerts Are Disabled");
  @ <p>Email alerts are disabled on this server</p>
  style_finish_page();
  return 1;
}

/*
** Insert a "Subscriber List" submenu link if the current user
** is an administrator.
*/
void alert_submenu_common(void){
  if( g.perm.Admin ){
    if( fossil_strcmp(g.zPath,"subscribers") ){
      style_submenu_element("Subscribers","%R/subscribers");
    }
    if( fossil_strcmp(g.zPath,"subscribe") ){
      style_submenu_element("Add New Subscriber","%R/subscribe");
    }
    if( fossil_strcmp(g.zPath,"setup_notification") ){
      style_submenu_element("Notification Setup","%R/setup_notification");
    }
  }
}


/*
** WEBPAGE: setup_notification
**
** Administrative page for configuring and controlling email notification.
** Normally accessible via the /Admin/Notification menu.
*/
void setup_notification(void){
  static const char *const azSendMethods[] = {
    "off",   "Disabled",
    "relay", "SMTP relay",
    "db",    "Store in a database",
    "dir",   "Store in a directory",
    "pipe",  "Pipe to a command",
  };
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  db_begin_transaction();

  alert_submenu_common();
  style_submenu_element("Send Announcement","%R/announce");
  style_set_current_feature("alerts");
  style_header("Email Notification Setup");
  @ <form action="%R/setup_notification" method="post"><div>
  @ <h1>Status &ensp; <input type="submit"  name="submit" value="Refresh"></h1>
  @ </form>
  @ <table class="label-value">
  if( alert_enabled() ){
    stats_for_email();
  }else{
    @ <th>Disabled</th>
  }
  @ </table>
  @ <hr>
  @ <form action="%R/setup_notification" method="post"><div>
  @ <h1> Configuration </h1>
  @ <p><input type="submit"  name="submit" value="Apply Changes"></p>
  @ <hr>
  login_insert_csrf_secret();

  entry_attribute("Canonical Server URL", 40, "email-url",
                   "eurl", "", 0);
  @ <p><b>Required.</b>
  @ This URL is used as the basename for hyperlinks included in
  @ email alert text.  Omit the trailing "/".
  @ Suggested value: "%h(g.zBaseURL)"
  @ (Property: "email-url")</p>
  @ <hr>

  entry_attribute("Administrator email address", 40, "email-admin",
                   "eadmin", "", 0);
  @ <p>This is the email for the human administrator for the system.
  @ Abuse and trouble reports and password reset requests are send here.
  @ (Property: "email-admin")</p>
  @ <hr>

  entry_attribute("\"Return-Path\" email address", 20, "email-self",
                   "eself", "", 0);
  @ <p><b>Required.</b>
  @ This is the email to which email notification bounces should be sent.
  @ In cases where the email notification does not align with a specific
  @ Fossil login account (for example, digest messages), this is also
  @ the "From:" address of the email notification.
  @ The system administrator should arrange for emails sent to this address
  @ to be handed off to the "fossil email incoming" command so that Fossil
  @ can handle bounces. (Property: "email-self")</p>
  @ <hr>

  entry_attribute("List-ID", 40, "email-listid",
                   "elistid", "", 0);
  @ <p>
  @ If this is not an empty string, then it becomes the argument to
  @ a "List-ID:" header on all out-bound notification emails.
  @ (Property: "email-listid")</p>
  @ <hr>

  entry_attribute("Repository Nickname", 16, "email-subname",
                   "enn", "", 0);
  @ <p><b>Required.</b>
  @ This is short name used to identifies the repository in the
  @ Subject: line of email alerts.  Traditionally this name is
  @ included in square brackets.  Examples: "[fossil-src]", "[sqlite-src]".
  @ (Property: "email-subname")</p>
  @ <hr>

  entry_attribute("Subscription Renewal Interval In Days", 8,
                  "email-renew-interval", "eri", "", 0);
  @ <p>
  @ If this value is an integer N greater than or equal to 14, then email
  @ notification subscriptions will be suspended N days after the last known
  @ interaction with the user.  This prevents sending notifications
  @ to abandoned accounts.  If a subscription comes within 7 days of expiring,
  @ a separate email goes out with the daily digest that prompts the
  @ subscriber to click on a link to the "/renew" webpage in order to
  @ extend their subscription.  Subscriptions never expire if this setting
  @ is less than 14 or is an empty string.
  @ (Property: "email-renew-interval")</p>
  @ <hr>

  multiple_choice_attribute("Email Send Method", "email-send-method", "esm",
       "off", count(azSendMethods)/2, azSendMethods);
  @ <p>How to send email.  Requires auxiliary information from the fields
  @ that follow.  Hint: Use the <a href="%R/announce">/announce</a> page
  @ to send test message to debug this setting.
  @ (Property: "email-send-method")</p>
  alert_schema(1);
  entry_attribute("SMTP Relay Host", 60, "email-send-relayhost",
                   "esrh", "localhost", 0);
  @ <p>When the send method is "SMTP relay", each email message is
  @ transmitted via the SMTP protocol (rfc5321) to a "Mail Submission
  @ Agent" or "MSA" (rfc4409) at the hostname shown here.  Optionally
  @ append a colon and TCP port number (ex: smtp.example.com:587).
  @ The default TCP port number is 25.
  @ Usage Hint:  If Fossil is running inside of a chroot jail, then it might
  @ not be able to resolve hostnames.  Work around this by using a raw IP
  @ address or create a "/etc/hosts" file inside the chroot jail.
  @ (Property: "email-send-relayhost")</p>
  @ 
  entry_attribute("Store Emails In This Database", 60, "email-send-db",
                   "esdb", "", 0);
  @ <p>When the send method is "store in a database", each email message is
  @ stored in an SQLite database file with the name given here.
  @ (Property: "email-send-db")</p>
  entry_attribute("Pipe Email Text Into This Command", 60, "email-send-command",
                   "ecmd", "sendmail -ti", 0);
  @ <p>When the send method is "pipe to a command", this is the command
  @ that is run.  Email messages are piped into the standard input of this
  @ command.  The command is expected to extract the sender address,
  @ recipient addresses, and subject from the header of the piped email
  @ text.  (Property: "email-send-command")</p>
  entry_attribute("Store Emails In This Directory", 60, "email-send-dir",
                   "esdir", "", 0);
  @ <p>When the send method is "store in a directory", each email message is
  @ stored as a separate file in the directory shown here.
  @ (Property: "email-send-dir")</p>

  @ <hr>

  @ <p><input type="submit"  name="submit" value="Apply Changes"></p>
  @ </div></form>
  db_end_transaction(0);
  style_finish_page();
}

#if 0
/*
** Encode pMsg as MIME base64 and append it to pOut
*/
static void append_base64(Blob *pOut, Blob *pMsg){
  int n, i, k;
  char zBuf[100];
  n = blob_size(pMsg);
  for(i=0; i<n; i+=54){
    k = translateBase64(blob_buffer(pMsg)+i, i+54<n ? 54 : n-i, zBuf);
    blob_append(pOut, zBuf, k);
    blob_append(pOut, "\r\n", 2);
  }
}
#endif

/*
** Encode pMsg using the quoted-printable email encoding and
** append it onto pOut
*/
static void append_quoted(Blob *pOut, Blob *pMsg){
  char *zIn = blob_str(pMsg);
  char c;
  int iCol = 0;
  while( (c = *(zIn++))!=0 ){
    if( (c>='!' && c<='~' && c!='=' && c!=':')
     || (c==' ' && zIn[0]!='\r' && zIn[0]!='\n')
    ){
      blob_append_char(pOut, c);
      iCol++;
      if( iCol>=70 ){
        blob_append(pOut, "=\r\n", 3);
        iCol = 0;
      }
    }else if( c=='\r' && zIn[0]=='\n' ){
      zIn++;
      blob_append(pOut, "\r\n", 2);
      iCol = 0;
    }else if( c=='\n' ){
      blob_append(pOut, "\r\n", 2);
      iCol = 0;
    }else{
      char x[3];
      x[0] = '=';
      x[1] = "0123456789ABCDEF"[(c>>4)&0xf];
      x[2] = "0123456789ABCDEF"[c&0xf];
      blob_append(pOut, x, 3);
      iCol += 3;
    }
  }
}

#if INTERFACE
/*
** An instance of the following object is used to send emails.
*/
struct AlertSender {
  sqlite3 *db;               /* Database emails are sent to */
  sqlite3_stmt *pStmt;       /* Stmt to insert into the database */
  const char *zDest;         /* How to send email. */
  const char *zDb;           /* Name of database file */
  const char *zDir;          /* Directory in which to store as email files */
  const char *zCmd;          /* Command to run for each email */
  const char *zFrom;         /* Emails come from here */
  const char *zListId;       /* Argument to List-ID header */
  SmtpSession *pSmtp;        /* SMTP relay connection */
  Blob out;                  /* For zDest=="blob" */
  char *zErr;                /* Error message */
  u32 mFlags;                /* Flags */
  int bImmediateFail;        /* On any error, call fossil_fatal() */
};

/* Allowed values for mFlags to alert_sender_new().
*/
#define ALERT_IMMEDIATE_FAIL   0x0001   /* Call fossil_fatal() on any error */
#define ALERT_TRACE            0x0002   /* Log sending process on console */

#endif /* INTERFACE */

/*
** Shutdown an emailer.  Clear all information other than the error message.
*/
static void emailerShutdown(AlertSender *p){
  sqlite3_finalize(p->pStmt);
  p->pStmt = 0;
  sqlite3_close(p->db);
  p->db = 0;
  p->zDb = 0;
  p->zDir = 0;
  p->zCmd = 0;
  p->zListId = 0;
  if( p->pSmtp ){
    smtp_client_quit(p->pSmtp);
    smtp_session_free(p->pSmtp);
    p->pSmtp = 0;
  }
  blob_reset(&p->out);
}

/*
** Put the AlertSender into an error state.
*/
static void emailerError(AlertSender *p, const char *zFormat, ...){
  va_list ap;
  fossil_free(p->zErr);
  va_start(ap, zFormat);
  p->zErr = vmprintf(zFormat, ap);
  va_end(ap);
  emailerShutdown(p);
  if( p->mFlags & ALERT_IMMEDIATE_FAIL ){
    fossil_fatal("%s", p->zErr);
  }
}

/*
** Free an email sender object
*/
void alert_sender_free(AlertSender *p){
  if( p ){
    emailerShutdown(p);
    fossil_free(p->zErr);
    fossil_free(p);
  }
}

/*
** Get an email setting value.  Report an error if not configured.
** Return 0 on success and one if there is an error.
*/
static int emailerGetSetting(
  AlertSender *p,        /* Where to report the error */
  const char **pzVal,    /* Write the setting value here */
  const char *zName      /* Name of the setting */
){
  const char *z = db_get(zName, 0);
  int rc = 0;
  if( z==0 || z[0]==0 ){
    emailerError(p, "missing \"%s\" setting", zName);
    rc = 1;
  }else{
    *pzVal = z;
  }
  return rc;
}

/*
** Create a new AlertSender object.
**
** The method used for sending email is determined by various email-*
** settings, and especially email-send-method.  The repository
** email-send-method can be overridden by the zAltDest argument to
** cause a different sending mechanism to be used.  Pass "stdout" to
** zAltDest to cause all emails to be printed to the console for
** debugging purposes.
**
** The AlertSender object returned must be freed using alert_sender_free().
*/
AlertSender *alert_sender_new(const char *zAltDest, u32 mFlags){
  AlertSender *p;

  p = fossil_malloc(sizeof(*p));
  memset(p, 0, sizeof(*p));
  blob_init(&p->out, 0, 0);
  p->mFlags = mFlags;
  if( zAltDest ){
    p->zDest = zAltDest;
  }else{
    p->zDest = db_get("email-send-method",0);
  }
  if( fossil_strcmp(p->zDest,"off")==0 ) return p;
  if( emailerGetSetting(p, &p->zFrom, "email-self") ) return p;
  p->zListId = db_get("email-listid", 0);
  if( fossil_strcmp(p->zDest,"db")==0 ){
    char *zErr;
    int rc;
    if( emailerGetSetting(p, &p->zDb, "email-send-db") ) return p;
    rc = sqlite3_open(p->zDb, &p->db);
    if( rc ){
      emailerError(p, "unable to open output database file \"%s\": %s",
                   p->zDb, sqlite3_errmsg(p->db));
      return p;
    }
    rc = sqlite3_exec(p->db, "CREATE TABLE IF NOT EXISTS email(\n"
                          "  emailid INTEGER PRIMARY KEY,\n"
                          "  msg TEXT\n);", 0, 0, &zErr);
    if( zErr ){
      emailerError(p, "CREATE TABLE failed with \"%s\"", zErr);
      sqlite3_free(zErr);
      return p;
    }
    rc = sqlite3_prepare_v2(p->db, "INSERT INTO email(msg) VALUES(?1)", -1,
                            &p->pStmt, 0);
    if( rc ){
      emailerError(p, "cannot prepare INSERT statement: %s",
                 sqlite3_errmsg(p->db));
      return p;
    }
  }else if( fossil_strcmp(p->zDest, "pipe")==0 ){
    emailerGetSetting(p, &p->zCmd, "email-send-command");
  }else if( fossil_strcmp(p->zDest, "dir")==0 ){
    emailerGetSetting(p, &p->zDir, "email-send-dir");
  }else if( fossil_strcmp(p->zDest, "blob")==0 ){
    blob_init(&p->out, 0, 0);
  }else if( fossil_strcmp(p->zDest, "relay")==0
         || fossil_strcmp(p->zDest, "debug-relay")==0
  ){
    const char *zRelay = 0;
    emailerGetSetting(p, &zRelay, "email-send-relayhost");
    if( zRelay ){
      u32 smtpFlags = SMTP_DIRECT;
      if( mFlags & ALERT_TRACE ) smtpFlags |= SMTP_TRACE_STDOUT;
      blob_init(&p->out, 0, 0);
      p->pSmtp = smtp_session_new(domain_of_addr(p->zFrom), zRelay,
                                  smtpFlags, 0);
      if( p->pSmtp==0 || p->pSmtp->zErr ){
        emailerError(p, "Could not start SMTP session: %s",
                        p->pSmtp ? p->pSmtp->zErr : "reason unknown");
      }else if( p->zDest[0]=='d' ){
        smtp_session_config(p->pSmtp, SMTP_TRACE_BLOB, &p->out);
      }
    }
  }
  return p;
}

/*
** Scan the header of the email message in pMsg looking for the
** (first) occurrance of zField.  Fill pValue with the content of
** that field.
**
** This routine initializes pValue.  Any prior content of pValue is
** discarded (leaked).
**
** Return non-zero on success.  Return 0 if no instance of the header
** is found.
*/
int email_header_value(Blob *pMsg, const char *zField, Blob *pValue){
  int nField = (int)strlen(zField);
  Blob line;
  blob_rewind(pMsg);
  blob_init(pValue,0,0);
  while( blob_line(pMsg, &line) ){
    int n, i;
    char *z;
    blob_trim(&line);
    n = blob_size(&line);
    if( n==0 ) return 0;
    if( n<nField+1 ) continue;
    z = blob_buffer(&line);
    if( sqlite3_strnicmp(z, zField, nField)==0 && z[nField]==':' ){
      for(i=nField+1; i<n && fossil_isspace(z[i]); i++){}
      blob_init(pValue, z+i, n-i);
      while( blob_line(pMsg, &line) ){
        blob_trim(&line);
        n = blob_size(&line);
        if( n==0 ) break;
        z = blob_buffer(&line);
        if( !fossil_isspace(z[0]) ) break;
        for(i=1; i<n && fossil_isspace(z[i]); i++){}
        blob_append(pValue, " ", 1);
        blob_append(pValue, z+i, n-i);
      }
      return 1;
    }
  }
  return 0;
}

/*
** Determine whether or not the input string is a valid email address.
** Only look at character up to but not including the first \000 or
** the first cTerm character, whichever comes first.
**
** Return the length of the email addresss string in bytes if the email
** address is valid.  If the email address is misformed, return 0.
*/
int email_address_is_valid(const char *z, char cTerm){
  int i;
  int nAt = 0;
  int nDot = 0;
  char c;
  if( z[0]=='.' ) return 0;  /* Local part cannot begin with "." */
  for(i=0; (c = z[i])!=0 && c!=cTerm; i++){
    if( fossil_isalnum(c) ){
      /* Alphanumerics are always ok */
    }else if( c=='@' ){
      if( nAt ) return 0;   /* Only a single "@"  allowed */
      if( i>64 ) return 0;  /* Local part too big */
      nAt = 1;
      nDot = 0;
      if( i==0 ) return 0;  /* Disallow empty local part */
      if( z[i-1]=='.' ) return 0; /* Last char of local cannot be "." */
      if( z[i+1]=='.' || z[i+1]=='-' ){
        return 0; /* Domain cannot begin with "." or "-" */
      }
    }else if( c=='-' ){
      if( z[i+1]==cTerm ) return 0;  /* Last character cannot be "-" */
    }else if( c=='.' ){
      if( z[i+1]=='.' ) return 0;  /* Do not allow ".." */
      if( z[i+1]==cTerm ) return 0;  /* Domain may not end with . */
      nDot++;
    }else if( (c=='_' || c=='+') && nAt==0 ){
      /* _ and + are ok in the local part */
    }else{
      return 0;   /* Anything else is an error */
    }
  }
  if( c!=cTerm ) return 0;    /* Missing terminator */
  if( nAt==0 ) return 0;      /* No "@" found anywhere */
  if( nDot==0 ) return 0;     /* No "." in the domain */
  return i;
}

/*
** Make a copy of the input string up to but not including the
** first cTerm character.
**
** Verify that the string to be copied really is a valid
** email address.  If it is not, then return NULL.
**
** This routine is more restrictive than necessary.  It does not
** allow comments, IP address, quoted strings, or certain uncommon
** characters.  The only non-alphanumerics allowed in the local
** part are "_", "+", "-" and "+".
*/
char *email_copy_addr(const char *z, char cTerm ){
  int i = email_address_is_valid(z, cTerm);
  return i==0 ? 0 : mprintf("%.*s", i, z);
}

/*
** Scan the input string for a valid email address that may be
** enclosed in <...>, or delimited by ',' or ':' or '=' or ' '.
** If the string contains one or more email addresses, extract the first
** one into memory obtained from mprintf() and return a pointer to it.
** If no valid email address can be found, return NULL.
*/
char *alert_find_emailaddr(const char *zIn){
  char *zOut = 0;
  do{
    zOut = email_copy_addr(zIn, zIn[strcspn(zIn, ">,:= ")]);
    if( zOut!=0 ) break;
    zIn = (const char *)strpbrk(zIn, "<,:= ");
    if( zIn==0 ) break;
    zIn++;
  }while( zIn!=0 );
  return zOut;
}

/*
** SQL function:  find_emailaddr(X)
**
** Return the first valid email address of the form <...> in input string
** X.  Or return NULL if not found.
*/
void alert_find_emailaddr_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zIn = (const char*)sqlite3_value_text(argv[0]);
  char *zOut = alert_find_emailaddr(zIn);
  if( zOut ){
    sqlite3_result_text(context, zOut, -1, fossil_free);
  }
}

/*
** SQL function:  display_name(X)
**
** If X is a string, search for a user name at the beginning of that
** string.  The user name must be followed by an email address.  If
** found, return the user name.  If not found, return NULL.
**
** This routine is used to extract the display name from the USER.INFO
** field.
*/
void alert_display_name_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const char *zIn = (const char*)sqlite3_value_text(argv[0]);
  int i;
  if( zIn==0 ) return;
  while( fossil_isspace(zIn[0]) ) zIn++;
  for(i=0; zIn[i] && zIn[i]!='<' && zIn[i]!='\n'; i++){}
  if( zIn[i]=='<' ){
    while( i>0 && fossil_isspace(zIn[i-1]) ){ i--; }
    if( i>0 ){
      sqlite3_result_text(context, zIn, i, SQLITE_TRANSIENT);
    }
  }
}

/*
** Return the hostname portion of an email address - the part following
** the @
*/
char *alert_hostname(const char *zAddr){
  char *z = strchr(zAddr, '@');
  if( z ){
    z++;
  }else{
    z = (char*)zAddr;
  }
  return z;
}

/*
** Return a pointer to a fake email mailbox name that corresponds
** to human-readable name zFromName.  The fake mailbox name is based
** on a hash.  No huge problems arise if there is a hash collisions,
** but it is still better if collisions can be avoided.
**
** The returned string is held in a static buffer and is overwritten
** by each subsequent call to this routine.
*/
static char *alert_mailbox_name(const char *zFromName){
  static char zHash[20];
  unsigned int x = 0;
  int n = 0;
  while( zFromName[0] ){
    n++;
    x = x*1103515245 + 12345 + ((unsigned char*)zFromName)[0];
    zFromName++;
  }
  sqlite3_snprintf(sizeof(zHash), zHash,
      "noreply%x%08x", n, x);
  return zHash;
}

/*
** COMMAND: test-mailbox-hashname
**
** Usage: %fossil test-mailbox-hashname HUMAN-NAME ...
**
** Return the mailbox hash name corresponding to each human-readable
** name on the command line.  This is a test interface for the
** alert_mailbox_name() function.
*/
void alert_test_mailbox_hashname(void){
  int i;
  for(i=2; i<g.argc; i++){
    fossil_print("%30s: %s\n", g.argv[i], alert_mailbox_name(g.argv[i]));
  }
}

/*
** Extract all To: header values from the email header supplied.
** Store them in the array list.
*/
void email_header_to(Blob *pMsg, int *pnTo, char ***pazTo){
  int nTo = 0;
  char **azTo = 0;
  Blob v;
  char *z, *zAddr;
  int i;

  email_header_value(pMsg, "to", &v);
  z = blob_str(&v);
  for(i=0; z[i]; i++){
    if( z[i]=='<' && (zAddr = email_copy_addr(&z[i+1],'>'))!=0 ){
      azTo = fossil_realloc(azTo, sizeof(azTo[0])*(nTo+1) );
      azTo[nTo++] = zAddr;
    }
  }
  *pnTo = nTo;
  *pazTo = azTo;
}

/*
** Free a list of To addresses obtained from a prior call to
** email_header_to()
*/
void email_header_to_free(int nTo, char **azTo){
  int i;
  for(i=0; i<nTo; i++) fossil_free(azTo[i]);
  fossil_free(azTo);
}

/*
** Send a single email message.
**
** The recipient(s) must be specified using  "To:" or "Cc:" or "Bcc:" fields
** in the header.  Likewise, the header must contains a "Subject:" line.
** The header might also include fields like "Message-Id:" or
** "In-Reply-To:".
**
** This routine will add fields to the header as follows:
**
**     From:
**     Date:
**     Message-Id:
**     Content-Type:
**     Content-Transfer-Encoding:
**     MIME-Version:
**     Sender:
**
** The caller maintains ownership of the input Blobs.  This routine will
** read the Blobs and send them onward to the email system, but it will
** not free them.
**
** The Message-Id: field is added if there is not already a Message-Id
** in the pHdr parameter.
**
** If the zFromName argument is not NULL, then it should be a human-readable
** name or handle for the sender.  In that case, "From:" becomes a made-up
** email address based on a hash of zFromName and the domain of email-self,
** and an additional "Sender:" field is inserted with the email-self
** address.  Downstream software might use the Sender header to set
** the envelope-from address of the email.  If zFromName is a NULL pointer,
** then the "From:" is set to the email-self value and Sender is
** omitted.
*/
void alert_send(
  AlertSender *p,           /* Emailer context */
  Blob *pHdr,               /* Email header (incomplete) */
  Blob *pBody,              /* Email body */
  const char *zFromName     /* Optional human-readable name of sender */
){
  Blob all, *pOut;
  u64 r1, r2;
  if( p->mFlags & ALERT_TRACE ){
    fossil_print("Sending email\n");
  }
  if( fossil_strcmp(p->zDest, "off")==0 ){
    return;
  }
  blob_init(&all, 0, 0);
  if( fossil_strcmp(p->zDest, "blob")==0 ){
    pOut = &p->out;
    if( blob_size(pOut) ){
      blob_appendf(pOut, "%.72c\n", '=');
    }
  }else{
    pOut = &all;
  }
  blob_append(pOut, blob_buffer(pHdr), blob_size(pHdr));
  if( p->zFrom==0 || p->zFrom[0]==0 ){
    return;  /* email-self is not set.  Error will be reported separately */
  }else if( zFromName ){
    blob_appendf(pOut, "From: %s <%s@%s>\r\n",
       zFromName, alert_mailbox_name(zFromName), alert_hostname(p->zFrom));
    blob_appendf(pOut, "Sender: <%s>\r\n", p->zFrom);
  }else{
    blob_appendf(pOut, "From: <%s>\r\n", p->zFrom);
  }
  blob_appendf(pOut, "Date: %z\r\n", cgi_rfc822_datestamp(time(0)));
  if( strstr(blob_str(pHdr), "\r\nMessage-Id:")==0 ){
    /* Message-id format:  "<$(date)x$(random)@$(from-host)>" where $(date) is
    ** the current unix-time in hex, $(random) is a 64-bit random number,
    ** and $(from) is the domain part of the email-self setting. */
    sqlite3_randomness(sizeof(r1), &r1);
    r2 = time(0);
    blob_appendf(pOut, "Message-Id: <%llxx%016llx@%s>\r\n",
                 r2, r1, alert_hostname(p->zFrom));
  }
  blob_add_final_newline(pBody);
  blob_appendf(pOut, "MIME-Version: 1.0\r\n");
  blob_appendf(pOut, "Content-Type: text/plain; charset=\"UTF-8\"\r\n");
#if 0
  blob_appendf(pOut, "Content-Transfer-Encoding: base64\r\n\r\n");
  append_base64(pOut, pBody);
#else
  blob_appendf(pOut, "Content-Transfer-Encoding: quoted-printable\r\n\r\n");
  append_quoted(pOut, pBody);
#endif
  if( p->pStmt ){
    int i, rc;
    sqlite3_bind_text(p->pStmt, 1, blob_str(&all), -1, SQLITE_TRANSIENT);
    for(i=0; i<100 && sqlite3_step(p->pStmt)==SQLITE_BUSY; i++){
      sqlite3_sleep(10);
    }
    rc = sqlite3_reset(p->pStmt);
    if( rc!=SQLITE_OK ){
      emailerError(p, "Failed to insert email message into output queue.\n"
                      "%s", sqlite3_errmsg(p->db));
    }
  }else if( p->zCmd ){
    FILE *out = popen(p->zCmd, "w");
    if( out ){
      fwrite(blob_buffer(&all), 1, blob_size(&all), out);
      pclose(out);
    }else{
      emailerError(p, "Could not open output pipe \"%s\"", p->zCmd);
    }
  }else if( p->zDir ){
    char *zFile = file_time_tempname(p->zDir, ".email");
    blob_write_to_file(&all, zFile);
    fossil_free(zFile);
  }else if( p->pSmtp ){
    char **azTo = 0;
    int nTo = 0;
    SmtpSession *pSmtp = p->pSmtp;
    email_header_to(pHdr, &nTo, &azTo);
    if( nTo>0 && !pSmtp->bFatal ){
      smtp_send_msg(pSmtp,p->zFrom,nTo,(const char**)azTo,blob_str(&all));
      if( pSmtp->zErr && !pSmtp->bFatal ){
        smtp_send_msg(pSmtp,p->zFrom,nTo,(const char**)azTo,blob_str(&all));
      }
      if( pSmtp->zErr ){
        fossil_errorlog("SMTP: (%s) %s", pSmtp->bFatal ? "fatal" : "retry",
                        pSmtp->zErr);
      }
      email_header_to_free(nTo, azTo);
    }
  }else if( strcmp(p->zDest, "stdout")==0 ){
    char **azTo = 0;
    int nTo = 0;
    int i;
    email_header_to(pHdr, &nTo, &azTo);
    for(i=0; i<nTo; i++){
      fossil_print("X-To-Test-%d: [%s]\r\n", i, azTo[i]);
    }
    email_header_to_free(nTo, azTo);
    blob_add_final_newline(&all);
    fossil_print("%s", blob_str(&all));
  }
  blob_reset(&all);
}

/*
** SETTING: email-url                 width=40
** This is the main URL used to access the repository for cloning or
** syncing or for operating the web interface.  It is also
** the basename for hyperlinks included in email alert text.
** Omit the trailing "/".  If the repository is not intended to be
** a long-running server and will not be sending email notifications,
** then leave this setting blank.
*/
/*
** SETTING: email-admin               width=40
** This is the email address for the human administrator for the system.
** Abuse and trouble reports and password reset requests are send here.
*/
/*
** SETTING: email-subname             width=16
** This is a short name used to identifies the repository in the Subject:
** line of email alerts. Traditionally this name is included in square
** brackets. Examples: "[fossil-src]", "[sqlite-src]".
*/
/*
** SETTING: email-renew-interval      width=16
** If this setting as an integer N that is 14 or greater then email
** notification is suspended for subscriptions that have a "last contact
** time" of more than N days ago.  The "last contact time" is recorded
** in the SUBSCRIBER.LASTCONTACT entry of the database.  Logging in,
** sending a forum post, editing a wiki page, changing subscription settings
** at /alerts, or visiting /renew all update the last contact time.
** If this setting is not an integer value or is less than 14 or undefined,
** then subscriptions never expire.
*/
/* X-VARIABLE:  email-renew-warning
** X-VARIABLE:  email-renew-cutoff
**
** These CONFIG table entries are not considered "settings" since their
** values are computed and updated automatically.
**
** email-renew-cutoff is the lastContact cutoff for subscription.  It
** is measured in days since 1970-01-01.  If The lastContact time for
** a subscription is less than email-renew-cutoff, then now new emails
** are sent to the subscriber.
**
** email-renew-warning is the time (in days since 1970-01-01) when the
** last batch of "your subscription is about to expire" emails were
** sent out.
**
** email-renew-cutoff is normally 7 days behind email-renew-warning.
*/
/*
** SETTING: email-send-method         width=5 default=off sensitive
** Determine the method used to send email.  Allowed values are
** "off", "relay", "pipe", "dir", "db", and "stdout".  The "off" value
** means no email is ever sent.  The "relay" value means emails are sent
** to an Mail Sending Agent using SMTP located at email-send-relayhost.
** The "pipe" value means email messages are piped into a command
** determined by the email-send-command setting. The "dir" value means
** emails are written to individual files in a directory determined
** by the email-send-dir setting.  The "db" value means that emails
** are added to an SQLite database named by the* email-send-db setting.
** The "stdout" value writes email text to standard output, for debugging.
*/
/*
** SETTING: email-send-command       width=40 sensitive
** This is a command to which outbound email content is piped when the
** email-send-method is set to "pipe".  The command must extract
** recipient, sender, subject, and all other relevant information
** from the email header.
*/
/*
** SETTING: email-send-dir           width=40 sensitive
** This is a directory into which outbound emails are written as individual
** files if the email-send-method is set to "dir".
*/
/*
** SETTING: email-send-db            width=40 sensitive
** This is an SQLite database file into which outbound emails are written
** if the email-send-method is set to "db".
*/
/*
** SETTING: email-self               width=40
** This is the email address for the repository.  Outbound emails add
** this email address as the "From:" field.
*/
/*
** SETTING: email-listid             width=40
** If this setting is not an empty string, then it becomes the argument to
** a "List-ID:" header that is added to all out-bound notification emails.
*/
/*
** SETTING: email-send-relayhost      width=40 sensitive default=127.0.0.1
** This is the hostname and TCP port to which output email messages
** are sent when email-send-method is "relay".  There should be an
** SMTP server configured as a Mail Submission Agent listening on the
** designated host and port and all times.
*/


/*
** COMMAND: alerts*                     abbrv-subcom
**
** Usage: %fossil alerts SUBCOMMAND ARGS...
**
** Subcommands:
**
**    pending                 Show all pending alerts.  Useful for debugging.
**
**    reset                   Hard reset of all email notification tables
**                            in the repository.  This erases all subscription
**                            information.  ** Use with extreme care **
**
**    send                    Compose and send pending email alerts.
**                            Some installations may want to do this via
**                            a cron-job to make sure alerts are sent
**                            in a timely manner.
**
**                            Options:
**                               --digest     Send digests
**                               --renewal    Send subscription renewal
**                                            notices
**                               --test       Write to standard output
**
**    settings [NAME VALUE]   With no arguments, list all email settings.
**                            Or change the value of a single email setting.
**
**    status                  Report on the status of the email alert
**                            subsystem
**
**    subscribers [PATTERN]   List all subscribers matching PATTERN.  Either
**                            LIKE or GLOB wildcards can be used in PATTERN.
**
**    test-message TO [OPTS]  Send a single email message using whatever
**                            email sending mechanism is currently configured.
**                            Use this for testing the email notification
**                            configuration.
**
**                            Options:
**                              --body FILENAME         Content from FILENAME
**                              --smtp-trace            Trace SMTP processing
**                              --stdout                Send msg to stdout
**                              -S|--subject SUBJECT    Message "subject:"
**
**    unsubscribe EMAIL       Remove a single subscriber with the given EMAIL.
*/
void alert_cmd(void){
  const char *zCmd;
  int nCmd;
  db_find_and_open_repository(0, 0);
  alert_schema(0);
  zCmd = g.argc>=3 ? g.argv[2] : "x";
  nCmd = (int)strlen(zCmd);
  if( strncmp(zCmd, "pending", nCmd)==0 ){
    Stmt q;
    verify_all_options();
    if( g.argc!=3 ) usage("pending");
    db_prepare(&q,"SELECT eventid, sentSep, sentDigest, sentMod"
                  "  FROM pending_alert");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%10s %7s %10s %7s\n",
         db_column_text(&q,0),
         db_column_int(&q,1) ? "sentSep" : "",
         db_column_int(&q,2) ? "sentDigest" : "",
         db_column_int(&q,3) ? "sentMod" : "");
    }
    db_finalize(&q);
  }else
  if( strncmp(zCmd, "reset", nCmd)==0 ){
    int c;
    int bForce = find_option("force","f",0)!=0;
    verify_all_options();
    if( bForce ){
      c = 'y';
    }else{
      Blob yn;
      fossil_print(
          "This will erase all content in the repository tables, thus\n"
          "deleting all subscriber information.  The information will be\n"
          "unrecoverable.\n");
      prompt_user("Continue? (y/N) ", &yn);
      c = blob_str(&yn)[0];
      blob_reset(&yn);
    }
    if( c=='y' ){
      alert_drop_trigger();
      db_multi_exec(
        "DROP TABLE IF EXISTS subscriber;\n"
        "DROP TABLE IF EXISTS pending_alert;\n"
        "DROP TABLE IF EXISTS alert_bounce;\n"
        /* Legacy */
        "DROP TABLE IF EXISTS alert_pending;\n"
        "DROP TABLE IF EXISTS subscription;\n"
      );
      alert_schema(0);
    }
  }else
  if( strncmp(zCmd, "send", nCmd)==0 ){
    u32 eFlags = 0;
    if( find_option("digest",0,0)!=0 ) eFlags |= SENDALERT_DIGEST;
    if( find_option("renewal",0,0)!=0 ) eFlags |= SENDALERT_RENEWAL;
    if( find_option("test",0,0)!=0 ){
      eFlags |= SENDALERT_PRESERVE|SENDALERT_STDOUT;
    }
    verify_all_options();
    alert_send_alerts(eFlags);
  }else
  if( strncmp(zCmd, "settings", nCmd)==0 ){
    int isGlobal = find_option("global",0,0)!=0;
    int nSetting;
    const Setting *pSetting = setting_info(&nSetting);
    db_open_config(1, 0);
    verify_all_options();
    if( g.argc!=3 && g.argc!=5 ) usage("setting [NAME VALUE]");
    if( g.argc==5 ){
      const char *zLabel = g.argv[3];
      if( strncmp(zLabel, "email-", 6)!=0
       || (pSetting = db_find_setting(zLabel, 1))==0 ){
        fossil_fatal("not a valid email setting: \"%s\"", zLabel);
      }
      db_set(pSetting->name/*works-like:""*/, g.argv[4], isGlobal);
      g.argc = 3;
    }
    pSetting = setting_info(&nSetting);
    for(; nSetting>0; nSetting--, pSetting++ ){
      if( strncmp(pSetting->name,"email-",6)!=0 ) continue;
      print_setting(pSetting, 0, 0);
    }
  }else
  if( strncmp(zCmd, "status", nCmd)==0 ){
    Stmt q;
    int iCutoff;
    int nSetting, n;
    static const char *zFmt = "%-29s %d\n";
    const Setting *pSetting = setting_info(&nSetting);
    db_open_config(1, 0);
    verify_all_options();
    if( g.argc!=3 ) usage("status");
    pSetting = setting_info(&nSetting);
    for(; nSetting>0; nSetting--, pSetting++ ){
      if( strncmp(pSetting->name,"email-",6)!=0 ) continue;
      print_setting(pSetting, 0, 0);
    }
    n = db_int(0,"SELECT count(*) FROM pending_alert WHERE NOT sentSep");
    fossil_print(zFmt/*works-like:"%s%d"*/, "pending-alerts", n);
    n = db_int(0,"SELECT count(*) FROM pending_alert WHERE NOT sentDigest");
    fossil_print(zFmt/*works-like:"%s%d"*/, "pending-digest-alerts", n);
    db_prepare(&q,
       "SELECT"
       " name,"
       " value,"
       " now()/86400-value,"
       " date(value*86400,'unixepoch')"
       " FROM repository.config"
       " WHERE name in ('email-renew-warning','email-renew-cutoff');");
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%-29s %-6d (%d days ago on %s)\n",
         db_column_text(&q, 0),
         db_column_int(&q, 1),
         db_column_int(&q, 2),
         db_column_text(&q, 3));
    }
    db_finalize(&q);
    n = db_int(0,"SELECT count(*) FROM subscriber");
    fossil_print(zFmt/*works-like:"%s%d"*/, "total-subscribers", n);
    iCutoff = db_get_int("email-renew-cutoff", 0);
    n = db_int(0, "SELECT count(*) FROM subscriber WHERE sverified"
                  " AND NOT sdonotcall AND length(ssub)>1"
                  " AND lastContact>=%d", iCutoff);
    fossil_print(zFmt/*works-like:"%s%d"*/, "active-subscribers", n);
  }else
  if( strncmp(zCmd, "subscribers", nCmd)==0 ){
    Stmt q;
    verify_all_options();
    if( g.argc!=3 && g.argc!=4 ) usage("subscribers [PATTERN]");
    if( g.argc==4 ){
      char *zPattern = g.argv[3];
      db_prepare(&q,
        "SELECT semail FROM subscriber"
        " WHERE semail LIKE '%%%q%%' OR suname LIKE '%%%q%%'"
        "  OR semail GLOB '*%q*' or suname GLOB '*%q*'"
        " ORDER BY semail",
        zPattern, zPattern, zPattern, zPattern);
    }else{
      db_prepare(&q,
        "SELECT semail FROM subscriber"
        " ORDER BY semail");
    }
    while( db_step(&q)==SQLITE_ROW ){
      fossil_print("%s\n", db_column_text(&q, 0));
    }
    db_finalize(&q);
  }else
  if( strncmp(zCmd, "test-message", nCmd)==0 ){
    Blob prompt, body, hdr;
    const char *zDest = find_option("stdout",0,0)!=0 ? "stdout" : 0;
    int i;
    u32 mFlags = ALERT_IMMEDIATE_FAIL;
    const char *zSubject = find_option("subject", "S", 1);
    const char *zSource = find_option("body", 0, 1);
    AlertSender *pSender;
    if( find_option("smtp-trace",0,0)!=0 ) mFlags |= ALERT_TRACE;
    verify_all_options();
    blob_init(&prompt, 0, 0);
    blob_init(&body, 0, 0);
    blob_init(&hdr, 0, 0);
    blob_appendf(&hdr,"To: ");
    for(i=3; i<g.argc; i++){
      if( i>3 ) blob_append(&hdr, ", ", 2);
      blob_appendf(&hdr, "<%s>", g.argv[i]);
    }
    blob_append(&hdr,"\r\n",2);
    if( zSubject==0 ) zSubject = "fossil alerts test-message";
    blob_appendf(&hdr, "Subject: %s\r\n", zSubject);
    if( zSource ){
      blob_read_from_file(&body, zSource, ExtFILE);
    }else{
      prompt_for_user_comment(&body, &prompt);
    }
    blob_add_final_newline(&body);
    pSender = alert_sender_new(zDest, mFlags);
    alert_send(pSender, &hdr, &body, 0);
    alert_sender_free(pSender);
    blob_reset(&hdr);
    blob_reset(&body);
    blob_reset(&prompt);
  }else
  if( strncmp(zCmd, "unsubscribe", nCmd)==0 ){
    verify_all_options();
    if( g.argc!=4 ) usage("unsubscribe EMAIL");
    db_multi_exec(
      "DELETE FROM subscriber WHERE semail=%Q", g.argv[3]);
  }else
  {
    usage("pending|reset|send|setting|status|"
          "subscribers|test-message|unsubscribe");
  }
}

/*
** Do error checking on a submitted subscription form.  Return TRUE
** if the submission is valid.  Return false if any problems are seen.
*/
static int subscribe_error_check(
  int *peErr,           /* Type of error */
  char **pzErr,         /* Error message text */
  int needCaptcha       /* True if captcha check needed */
){
  const char *zEAddr;
  int i, j, n;
  char c;

  *peErr = 0;
  *pzErr = 0;

  /* Verify the captcha first */
  if( needCaptcha ){
    if( !captcha_is_correct(1) ){
      *peErr = 2;
      *pzErr = mprintf("incorrect security code");
      return 0;
    }
  }

  /* Check the validity of the email address.
  **
  **  (1) Exactly one '@' character.
  **  (2) No other characters besides [a-zA-Z0-9._+-]
  **
  **  The local part is currently more restrictive than RFC 5322 allows:
  **  https://stackoverflow.com/a/2049510/142454  We will expand this as
  **  necessary.
  */
  zEAddr = P("e");
  if( zEAddr==0 ){
    *peErr = 1;
    *pzErr = mprintf("required");
    return 0;
  }
  for(i=j=n=0; (c = zEAddr[i])!=0; i++){
    if( c=='@' ){
      n = i;
      j++;
      continue;
    }
    if( !fossil_isalnum(c) && c!='.' && c!='_' && c!='-' && c!='+' ){
      *peErr = 1;
      *pzErr = mprintf("illegal character in email address: 0x%x '%c'",
                   c, c);
      return 0;
    }
  }
  if( j!=1 ){
    *peErr = 1;
    *pzErr = mprintf("email address should contain exactly one '@'");
    return 0;
  }
  if( n<1 ){
    *peErr = 1;
    *pzErr = mprintf("name missing before '@' in email address");
    return 0;
  }
  if( n>i-5 ){
    *peErr = 1;
    *pzErr = mprintf("email domain too short");
     return 0;
  }

  if( authorized_subscription_email(zEAddr)==0 ){
    *peErr = 1;
    *pzErr = mprintf("not an authorized email address");
    return 0;
  }

  /* Check to make sure the email address is available for reuse */
  if( db_exists("SELECT 1 FROM subscriber WHERE semail=%Q", zEAddr) ){
    *peErr = 1;
    *pzErr = mprintf("this email address is used by someone else");
    return 0;
  }

  /* If we reach this point, all is well */
  return 1;
}

/*
** Text of email message sent in order to confirm a subscription.
*/
static const char zConfirmMsg[] =
@ Someone has signed you up for email alerts on the Fossil repository
@ at %s.
@
@ To confirm your subscription and begin receiving alerts, click on
@ the following hyperlink:
@
@    %s/alerts/%s
@
@ Save the hyperlink above!  You can reuse this same hyperlink to
@ unsubscribe or to change the kinds of alerts you receive.
@
@ If you do not want to subscribe, you can simply ignore this message.
@ You will not be contacted again.
@
;

/*
** Append the text of an email confirmation message to the given
** Blob.  The security code is in zCode.
*/
void alert_append_confirmation_message(Blob *pMsg, const char *zCode){
  blob_appendf(pMsg, zConfirmMsg/*works-like:"%s%s%s"*/,
                   g.zBaseURL, g.zBaseURL, zCode);
}

/*
** WEBPAGE: subscribe
**
** Allow users to subscribe to email notifications.
**
** This page is usually run by users who are not logged in.
** A logged-in user can add email notifications on the /alerts page.
** Access to this page by a logged in user (other than an
** administrator) results in a redirect to the /alerts page.
**
** Administrators can visit this page in order to sign up other
** users.
**
** The Alerts permission ("7") is required to access this
** page.  To allow anonymous passers-by to sign up for email
** notification, set Email-Alerts on user "nobody" or "anonymous".
*/
void subscribe_page(void){
  int needCaptcha;
  unsigned int uSeed = 0;
  const char *zDecoded;
  char *zCaptcha = 0;
  char *zErr = 0;
  int eErr = 0;
  int di;

  if( alert_webpages_disabled() ) return;
  login_check_credentials();
  if( !g.perm.EmailAlert ){
    login_needed(g.anon.EmailAlert);
    return;
  }
  if( login_is_individual()
   && db_exists("SELECT 1 FROM subscriber WHERE suname=%Q",g.zLogin)
  ){
    /* This person is already signed up for email alerts.  Jump
    ** to the screen that lets them edit their alert preferences.
    ** Except, administrators can create subscriptions for others so
    ** do not jump for them.
    */
    if( g.perm.Admin ){
      /* Admins get a link to admin their own account, but they
      ** stay on this page so that they can create subscriptions
      ** for other people. */
      style_submenu_element("My Subscription","%R/alerts");
    }else{
      /* Everybody else jumps to the page to administer their own
      ** account only. */
      cgi_redirectf("%R/alerts");
      return;
    }
  }
  if( !g.perm.Admin && !db_get_boolean("anon-subscribe",1) ){
    register_page();
    return;
  }
  style_set_current_feature("alerts");
  alert_submenu_common();
  needCaptcha = !login_is_individual();
  if( P("submit")
   && cgi_csrf_safe(2)
   && subscribe_error_check(&eErr,&zErr,needCaptcha)
  ){
    /* A validated request for a new subscription has been received. */
    char ssub[20];
    const char *zEAddr = P("e");
    const char *zCode;  /* New subscriber code (in hex) */
    int nsub = 0;
    const char *suname = PT("suname");
    if( suname==0 && needCaptcha==0 && !g.perm.Admin ) suname = g.zLogin;
    if( suname && suname[0]==0 ) suname = 0;
    if( PB("sa") ) ssub[nsub++] = 'a';
    if( g.perm.Read && PB("sc") )    ssub[nsub++] = 'c';
    if( g.perm.RdForum && PB("sf") ) ssub[nsub++] = 'f';
    if( g.perm.RdForum && PB("sn") ) ssub[nsub++] = 'n';
    if( g.perm.RdForum && PB("sr") ) ssub[nsub++] = 'r';
    if( g.perm.RdTkt && PB("st") )   ssub[nsub++] = 't';
    if( g.perm.Admin && PB("su") )   ssub[nsub++] = 'u';
    if( g.perm.RdWiki && PB("sw") )  ssub[nsub++] = 'w';
    if( g.perm.RdForum && PB("sx") ) ssub[nsub++] = 'x';
    ssub[nsub] = 0;
    zCode = db_text(0,
      "INSERT INTO subscriber(semail,suname,"
      "  sverified,sdonotcall,sdigest,ssub,sctime,mtime,smip,lastContact)"
      "VALUES(%Q,%Q,%d,0,%d,%Q,now(),now(),%Q,now()/86400)"
      "RETURNING hex(subscriberCode);",
      /* semail */    zEAddr,
      /* suname */    suname,
      /* sverified */ needCaptcha==0,
      /* sdigest */   PB("di"),
      /* ssub */      ssub,
      /* smip */      g.zIpAddr
    );
    if( !needCaptcha ){
      /* The new subscription has been added on behalf of a logged-in user.
      ** No verification is required.  Jump immediately to /alerts page.
      */
      if( g.perm.Admin ){
        cgi_redirectf("%R/alerts/%.32s", zCode);
      }else{
        cgi_redirectf("%R/alerts");
      }
      return;
    }else{
      /* We need to send a verification email */
      Blob hdr, body;
      AlertSender *pSender = alert_sender_new(0,0);
      blob_init(&hdr,0,0);
      blob_init(&body,0,0);
      blob_appendf(&hdr, "To: <%s>\n", zEAddr);
      blob_appendf(&hdr, "Subject: Subscription verification\n");
      alert_append_confirmation_message(&body, zCode);
      alert_send(pSender, &hdr, &body, 0);
      style_header("Email Alert Verification");
      if( pSender->zErr ){
        @ <h1>Internal Error</h1>
        @ <p>The following internal error was encountered while trying
        @ to send the confirmation email:
        @ <blockquote><pre>
        @ %h(pSender->zErr)
        @ </pre></blockquote>
      }else{
        @ <p>An email has been sent to "%h(zEAddr)". That email contains a
        @ hyperlink that you must click to activate your
        @ subscription.</p>
      }
      alert_sender_free(pSender);
      style_finish_page();
    }
    return;
  }
  style_header("Signup For Email Alerts");
  if( P("submit")==0 ){
    /* If this is the first visit to this page (if this HTTP request did not
    ** come from a prior Submit of the form) then default all of the
    ** subscription options to "on" */
    cgi_set_parameter_nocopy("sa","1",1);
    if( g.perm.Read )    cgi_set_parameter_nocopy("sc","1",1);
    if( g.perm.RdForum ) cgi_set_parameter_nocopy("sf","1",1);
    if( g.perm.RdForum ) cgi_set_parameter_nocopy("sn","1",1);
    if( g.perm.RdForum ) cgi_set_parameter_nocopy("sr","1",1);
    if( g.perm.RdTkt )   cgi_set_parameter_nocopy("st","1",1);
    if( g.perm.Admin )   cgi_set_parameter_nocopy("su","1",1);
    if( g.perm.RdWiki )  cgi_set_parameter_nocopy("sw","1",1);
  }
  @ <p>To receive email notifications for changes to this
  @ repository, fill out the form below and press the "Submit" button.</p>
  form_begin(0, "%R/subscribe");
  @ <table class="subscribe">
  @ <tr>
  @  <td class="form_label">Email&nbsp;Address:</td>
  @  <td><input type="text" name="e" value="%h(PD("e",""))" size="30"></td>
  @ <tr>
  if( eErr==1 ){
    @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
  }
  @ </tr>
  if( needCaptcha ){
    const char *zInit = "";
    if( P("captchaseed")!=0 && eErr!=2 ){
      uSeed = strtoul(P("captchaseed"),0,10);
      zInit = P("captcha");
    }else{
      uSeed = captcha_seed();
    }
    zDecoded = captcha_decode(uSeed, 0);
    zCaptcha = captcha_render(zDecoded);
    @ <tr>
    @  <td class="form_label">Security Code:</td>
    @  <td><input type="text" name="captcha" value="%h(zInit)" size="30">
    captcha_speakit_button(uSeed, "Speak the code");
    @  <input type="hidden" name="captchaseed" value="%u(uSeed)"></td>
    @ </tr>
    if( eErr==2 ){
      @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
    }
    @ </tr>
  }
  if( g.perm.Admin ){
    @ <tr>
    @  <td class="form_label">User:</td>
    @  <td><input type="text" name="suname" value="%h(PD("suname",g.zLogin))" \
    @  size="30"></td>
    @ </tr>
    if( eErr==3 ){
      @ <tr><td><td><span class='loginError'>&uarr; %h(zErr)</span></td></tr>
    }
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Topics:</td>
  @  <td><label><input type="checkbox" name="sa" %s(PCK("sa"))> \
  @  Announcements</label><br>
  if( g.perm.Read ){
    @  <label><input type="checkbox" name="sc" %s(PCK("sc"))> \
    @  Check-ins</label><br>
  }
  if( g.perm.RdForum ){
    @  <label><input type="checkbox" name="sf" %s(PCK("sf"))> \
    @  All Forum Posts</label><br>
    @  <label><input type="checkbox" name="sn" %s(PCK("sn"))> \
    @  New Forum Threads</label><br>
    @  <label><input type="checkbox" name="sr" %s(PCK("sr"))> \
    @  Replies To My Forum Posts</label><br>
    @  <label><input type="checkbox" name="sx" %s(PCK("sx"))> \
    @  Edits To Forum Posts</label><br>
  }
  if( g.perm.RdTkt ){
    @  <label><input type="checkbox" name="st" %s(PCK("st"))> \
    @  Ticket changes</label><br>
  }
  if( g.perm.RdWiki ){
    @  <label><input type="checkbox" name="sw" %s(PCK("sw"))> \
    @  Wiki</label><br>
  }
  if( g.perm.Admin ){
    @  <label><input type="checkbox" name="su" %s(PCK("su"))> \
    @  User permission changes</label>
  }
  di = PB("di");
  @ </td></tr>
  @ <tr>
  @  <td class="form_label">Delivery:</td>
  @  <td><select size="1" name="di">
  @     <option value="0" %s(di?"":"selected")>Individual Emails</option>
  @     <option value="1" %s(di?"selected":"")>Daily Digest</option>
  @     </select></td>
  @ </tr>
  if( g.perm.Admin ){
    @ <tr>
    @  <td class="form_label">Admin Options:</td><td>
    @  <label><input type="checkbox" name="vi" %s(PCK("vi"))> \
    @  Verified</label><br>
    @  <label><input type="checkbox" name="dnc" %s(PCK("dnc"))> \
    @  Do not call</label></td></tr>
  }
  @ <tr>
  @  <td></td>
  if( needCaptcha && !alert_enabled() ){
    @  <td><input type="submit" name="submit" value="Submit" disabled>
    @  (Email current disabled)</td>
  }else{
    @  <td><input type="submit" name="submit" value="Submit"></td>
  }
  @ </tr>
  @ </table>
  if( needCaptcha ){
    @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
    @ %h(zCaptcha)
    @ </pre>
    @ Enter the 8 characters above in the "Security Code" box<br/>
    @ </td></tr></table></div>
  }
  @ </form>
  fossil_free(zErr);
  style_finish_page();
}

/*
** Either shutdown or completely delete a subscription entry given
** by the hex value zName.  Then paint a webpage that explains that
** the entry has been removed.
*/
static void alert_unsubscribe(int sid, int bTotal){
  const char *zEmail = 0;
  const char *zLogin = 0;
  int uid = 0;
  Stmt q;
  db_prepare(&q, "SELECT semail, suname FROM subscriber"
                 " WHERE subscriberId=%d", sid);
  if( db_step(&q)==SQLITE_ROW ){
    zEmail = db_column_text(&q, 0);
    zLogin = db_column_text(&q, 1);
    uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", zLogin);
  }
  style_set_current_feature("alerts");
  if( zEmail==0 ){
    style_header("Unsubscribe Fail");
    @ <p>Unable to locate a subscriber with the requested key</p>
  }else{
    db_unprotect(PROTECT_READONLY);
    if( bTotal ){
      /* Completely delete the subscriber */
      db_multi_exec(
        "DELETE FROM subscriber WHERE subscriberId=%d", sid
      );
    }else{
      /* Keep the subscriber, but turn off all notifications */
      db_multi_exec(
        "UPDATE subscriber SET ssub='k', mtime=now() WHERE subscriberId=%d",
        sid
      );
    }
    db_protect_pop();
    style_header("Unsubscribed");
    @ <p>The "%h(zEmail)" email address has been unsubscribed from all
    @ notifications.  All subscription records for "%h(zEmail)" have
    @ been purged.  No further emails will be sent to "%h(zEmail)".</p>
    if( uid && g.perm.Admin ){
       @ <p>You may also want to
       @ <a href="%R/setup_uedit?id=%d(uid)">edit or delete
       @ the corresponding user "%h(zLogin)"</a></p>
    }
  }
  db_finalize(&q);
  style_finish_page();
  return;
}

/*
** WEBPAGE: alerts
**
** Edit email alert and notification settings.
**
** The subscriber is identified in several ways:
**
**    *    The name= query parameter contains the complete subscriberCode.
**         This only happens when the user receives a verification
**         email and clicks on the link in the email.  When a
**         compilete subscriberCode is seen on the name= query parameter,
**         that constitutes verification of the email address.
**
**    *    The sid= query parameter contains an integer subscriberId.
**         This only works for the administrator.  It allows the
**         administrator to edit any subscription.
**
**    *    The user is logged into an account other than "nobody" or
**         "anonymous".  In that case the notification settings
**         associated with that account can be edited without needing
**         to know the subscriber code.
**
**    *    The name= query parameter contains a 32-digit prefix of
**         subscriber code.  (Subscriber codes are normally 64 hex digits
**         in length.) This uniquely identifies the subscriber without
**         revealing the complete subscriber code, and hence without
**         verifying the email address.
*/
void alert_page(void){
  const char *zName = 0;        /* Value of the name= query parameter */
  Stmt q;                       /* For querying the database */
  int sa, sc, sf, st, su, sw, sx; /* Types of notifications requested */
  int sn, sr;
  int sdigest = 0, sdonotcall = 0, sverified = 0;  /* Other fields */
  int isLogin;                  /* True if logged in as an individual */
  const char *ssub = 0;         /* Subscription flags */
  const char *semail = 0;       /* Email address */
  const char *smip;             /* */
  const char *suname = 0;       /* Corresponding user.login value */
  const char *mtime;            /* */
  const char *sctime;           /* Time subscription created */
  int eErr = 0;                 /* Type of error */
  char *zErr = 0;               /* Error message text */
  int sid = 0;                  /* Subscriber ID */
  int nName;                    /* Length of zName in bytes */
  char *zHalfCode;              /* prefix of subscriberCode */
  int keepAlive = 0;            /* True to update the last contact time */

  db_begin_transaction();
  if( alert_webpages_disabled() ){
    db_commit_transaction();
    return;
  }
  login_check_credentials();
  isLogin = login_is_individual();
  zName = P("name");
  nName = zName ? (int)strlen(zName) : 0;
  if( g.perm.Admin && P("sid")!=0 ){
    sid = atoi(P("sid"));
  }
  if( sid==0 && nName>=32 ){
    sid = db_int(0,
       "SELECT CASE WHEN hex(subscriberCode) LIKE (%Q||'%%')"
       "            THEN subscriberId ELSE 0 END"
       "  FROM subscriber WHERE subscriberCode>=hextoblob(%Q)"
       " LIMIT 1", zName, zName);
    if( sid ) keepAlive = 1;
  }
  if( sid==0 && isLogin && g.perm.EmailAlert ){
    sid = db_int(0, "SELECT subscriberId FROM subscriber"
                    " WHERE suname=%Q", g.zLogin);
  }
  if( sid==0 ){
    db_commit_transaction();
    cgi_redirect("subscribe");
    /*NOTREACHED*/
  }
  alert_submenu_common();
  if( P("submit")!=0 && cgi_csrf_safe(2) ){
    char newSsub[10];
    int nsub = 0;
    Blob update;

    sdonotcall = PB("sdonotcall");
    sdigest = PB("sdigest");
    semail = P("semail");
    if( PB("sa") )                   newSsub[nsub++] = 'a';
    if( g.perm.Read && PB("sc") )    newSsub[nsub++] = 'c';
    if( g.perm.RdForum && PB("sf") ) newSsub[nsub++] = 'f';
    if( g.perm.RdForum && PB("sn") ) newSsub[nsub++] = 'n';
    if( g.perm.RdForum && PB("sr") ) newSsub[nsub++] = 'r';
    if( g.perm.RdTkt && PB("st") )   newSsub[nsub++] = 't';
    if( g.perm.Admin && PB("su") )   newSsub[nsub++] = 'u';
    if( g.perm.RdWiki && PB("sw") )  newSsub[nsub++] = 'w';
    if( g.perm.RdForum && PB("sx") ) newSsub[nsub++] = 'x';
    newSsub[nsub] = 0;
    ssub = newSsub;
    blob_init(&update, "UPDATE subscriber SET", -1);
    blob_append_sql(&update,
        " sdonotcall=%d,"
        " sdigest=%d,"
        " ssub=%Q,"
        " mtime=now(),"
        " lastContact=now()/86400,"
        " smip=%Q",
        sdonotcall,
        sdigest,
        ssub,
        g.zIpAddr
    );
    if( g.perm.Admin ){
      suname = PT("suname");
      sverified = PB("sverified");
      if( suname && suname[0]==0 ) suname = 0;
      blob_append_sql(&update,
        ", suname=%Q,"
        " sverified=%d",
        suname,
        sverified
      );
    }
    if( isLogin ){
      if( semail==0 || email_address_is_valid(semail,0)==0 ){
        eErr = 8;
      }
      blob_append_sql(&update, ", semail=%Q", semail);
    }
    blob_append_sql(&update," WHERE subscriberId=%d", sid);
    if( eErr==0 ){
      db_exec_sql(blob_str(&update));
      ssub = 0;
    }
    blob_reset(&update);
  }else if( keepAlive ){
    db_unprotect(PROTECT_READONLY);
    db_multi_exec(
      "UPDATE subscriber SET lastContact=now()/86400"
      " WHERE subscriberId=%d", sid
    );
    db_protect_pop();
  }
  if( P("delete")!=0 && cgi_csrf_safe(2) ){
    if( !PB("dodelete") ){
      eErr = 9;
      zErr = mprintf("Select this checkbox and press \"Unsubscribe\" again to"
                     " unsubscribe");
    }else{
      alert_unsubscribe(sid, 1);
      db_commit_transaction();
      return;
    }
  }
  style_set_current_feature("alerts");
  style_header("Update Subscription");
  db_prepare(&q,
    "SELECT"
    "  semail,"                       /* 0 */
    "  sverified,"                    /* 1 */
    "  sdonotcall,"                   /* 2 */
    "  sdigest,"                      /* 3 */
    "  ssub,"                         /* 4 */
    "  smip,"                         /* 5 */
    "  suname,"                       /* 6 */
    "  datetime(mtime,'unixepoch'),"  /* 7 */
    "  datetime(sctime,'unixepoch')," /* 8 */
    "  hex(subscriberCode),"          /* 9 */
    "  date(coalesce(lastContact*86400,mtime),'unixepoch'),"  /* 10 */
    "  now()/86400 - coalesce(lastContact,mtime/86400)"       /* 11 */
    " FROM subscriber WHERE subscriberId=%d", sid);
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    db_commit_transaction();
    cgi_redirect("subscribe");
    /*NOTREACHED*/
  }
  if( ssub==0 ){
    semail = db_column_text(&q, 0);
    sdonotcall = db_column_int(&q, 2);
    sdigest = db_column_int(&q, 3);
    ssub = db_column_text(&q, 4);
  }
  if( suname==0 ){
    suname = db_column_text(&q, 6);
    sverified = db_column_int(&q, 1);
  }
  sa = strchr(ssub,'a')!=0;
  sc = strchr(ssub,'c')!=0;
  sf = strchr(ssub,'f')!=0;
  sn = strchr(ssub,'n')!=0;
  sr = strchr(ssub,'r')!=0;
  st = strchr(ssub,'t')!=0;
  su = strchr(ssub,'u')!=0;
  sw = strchr(ssub,'w')!=0;
  sx = strchr(ssub,'x')!=0;
  smip = db_column_text(&q, 5);
  mtime = db_column_text(&q, 7);
  sctime = db_column_text(&q, 8);
  if( !g.perm.Admin && !sverified ){
    if( nName==64 ){
      db_unprotect(PROTECT_READONLY);
      db_multi_exec(
        "UPDATE subscriber SET sverified=1"
        " WHERE subscriberCode=hextoblob(%Q)",
        zName);
      db_protect_pop();
      if( db_get_boolean("selfreg-verify",0) ){
        char *zNewCap = db_get("default-perms","u");
        db_unprotect(PROTECT_USER);
        db_multi_exec(
           "UPDATE user"
           "   SET cap=%Q"
           " WHERE cap='7' AND login=("
           "   SELECT suname FROM subscriber"
           "    WHERE subscriberCode=hextoblob(%Q))",
           zNewCap, zName
        );
        db_protect_pop();
        login_set_capabilities(zNewCap, 0);
      }
      @ <h1>Your email alert subscription has been verified!</h1>
      @ <p>Use the form below to update your subscription information.</p>
      @ <p>Hint:  Bookmark this page so that you can more easily update
      @ your subscription information in the future</p>
    }else{
      @ <h2>Your email address is unverified</h2>
      @ <p>You should have received an email message containing a link
      @ that you must visit to verify your account.  No email notifications
      @ will be sent until your email address has been verified.</p>
    }
  }else{
    @ <p>Make changes to the email subscription shown below and
    @ press "Submit".</p>
  }
  form_begin(0, "%R/alerts");
  zHalfCode = db_text("x","SELECT hex(substr(subscriberCode,1,16))"
                          "  FROM subscriber WHERE subscriberId=%d", sid);
  @ <input type="hidden" name="name" value="%h(zHalfCode)">
  @ <table class="subscribe">
  @ <tr>
  @  <td class="form_label">Email&nbsp;Address:</td>
  if( isLogin ){
    @  <td><input type="text" name="semail" value="%h(semail)" size="30">\
    if( eErr==8 ){
      @ <span class='loginError'>&larr; not a valid email address!</span>
    }else if( g.perm.Admin ){
      @ &nbsp;&nbsp;<a href="%R/announce?to=%t(semail)">\
      @ (Send a message to %h(semail))</a>\
    }
    @ </td>
  }else{
    @  <td>%h(semail)</td>
  }
  @ </tr>
  if( g.perm.Admin ){
    int uid;
    @ <tr>
    @  <td class='form_label'>Created:</td>
    @  <td>%h(sctime)</td>
    @ </tr>
    @ <tr>
    @  <td class='form_label'>Last Modified:</td>
    @  <td>%h(mtime)</td>
    @ </tr>
    @ <tr>
    @  <td class='form_label'>IP Address:</td>
    @  <td>%h(smip)</td>
    @ </tr>
    @ <tr>
    @  <td class='form_label'>Subscriber&nbsp;Code:</td>
    @  <td>%h(db_column_text(&q,9))</td>
    @ <tr>
    @ <tr>
    @  <td class='form_label'>Last Contact:</td>
    @  <td>%h(db_column_text(&q,10)) &larr; \
    @      %,d(db_column_int(&q,11)) days ago</td>
    @ </tr>
    @  <td class="form_label">User:</td>
    @  <td><input type="text" name="suname" value="%h(suname?suname:"")" \
    @  size="30">\
    uid = db_int(0, "SELECT uid FROM user WHERE login=%Q", suname);
    if( uid ){
      @ &nbsp;&nbsp;<a href='%R/setup_uedit?id=%d(uid)'>\
      @ (login info for %h(suname))</a>\
    }
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Topics:</td>
  @  <td><label><input type="checkbox" name="sa" %s(sa?"checked":"")>\
  @  Announcements</label><br>
  if( g.perm.Read ){
    @  <label><input type="checkbox" name="sc" %s(sc?"checked":"")>\
    @  Check-ins</label><br>
  }
  if( g.perm.RdForum ){
    @  <label><input type="checkbox" name="sf" %s(sf?"checked":"")>\
    @  All Forum Posts</label><br>
    @  <label><input type="checkbox" name="sn" %s(sn?"checked":"")>\
    @  New Forum Threads</label><br>
    @  <label><input type="checkbox" name="sr" %s(sr?"checked":"")>\
    @  Replies To My Posts</label><br>
    @  <label><input type="checkbox" name="sx" %s(sx?"checked":"")>\
    @  Edits To Forum Posts</label><br>
  }
  if( g.perm.RdTkt ){
    @  <label><input type="checkbox" name="st" %s(st?"checked":"")>\
    @  Ticket changes</label><br>
  }
  if( g.perm.RdWiki ){
    @  <label><input type="checkbox" name="sw" %s(sw?"checked":"")>\
    @  Wiki</label><br>
  }
  if( g.perm.Admin ){
    /* Corner-case bug: if an admin assigns 'u' to a non-admin, that
    ** subscription will get removed if the user later edits their
    ** subscriptions, as non-admins are not permitted to add that
    ** subscription. */
    @  <label><input type="checkbox" name="su" %s(su?"checked":"")>\
    @  User permission changes</label>
  }
  @ </td></tr>
  if( strchr(ssub,'k')!=0 ){
    @ <tr><td></td><td>&nbsp;&uarr;&nbsp;
    @ Note: User did a one-click unsubscribe</td></tr>
  }
  @ <tr>
  @  <td class="form_label">Delivery:</td>
  @  <td><select size="1" name="sdigest">
  @     <option value="0" %s(sdigest?"":"selected")>Individual Emails</option>
  @     <option value="1" %s(sdigest?"selected":"")>Daily Digest</option>
  @     </select></td>
  @ </tr>
  if( g.perm.Admin ){
    @ <tr>
    @  <td class="form_label">Admin Options:</td><td>
    @  <label><input type="checkbox" name="sdonotcall" \
    @  %s(sdonotcall?"checked":"")> Do not disturb</label><br>
    @  <label><input type="checkbox" name="sverified" \
    @  %s(sverified?"checked":"")>\
    @  Verified</label></td></tr>
  }
  if( eErr==9 ){
    @ <tr>
    @  <td class="form_label">Verify:</td><td>
    @  <label><input type="checkbox" name="dodelete">
    @  Unsubscribe</label>
    @ <span class="loginError">&larr; %h(zErr)</span>
    @ </td></tr>
  }
  @ <tr>
  @  <td></td>
  @  <td><input type="submit" name="submit" value="Submit">
  @  <input type="submit" name="delete" value="Unsubscribe">
  @ </tr>
  @ </table>
  @ </form>
  fossil_free(zErr);
  db_finalize(&q);
  style_finish_page();
  db_commit_transaction();
  return;
}

/*
** WEBPAGE: renew
**
** Users visit this page to update the last-contact date on their
** subscription.  The last-contact date is the day that the subscriber
** last interacted with the repository.  If the name= query parameter
** (or POST parameter) contains a valid subscriber code, then the last-contact
** subscription associated with that subscriber code is updated to be the
** current date.
*/
void renewal_page(void){
  const char *zName = P("name");
  int iInterval = db_get_int("email-renew-interval", 0);
  Stmt s;
  int rc;

  style_header("Subscription Renewal");
  if( zName==0 || strlen(zName)<4 ){
    @ <p>No subscription specified</p>
    style_finish_page();
    return;
  }

  if( !db_table_has_column("repository","subscriber","lastContact")
   || iInterval<1
  ){
    @ <p>This repository does not expire email notification subscriptions.
    @ No renewals are necessary.</p>
    style_finish_page();
    return;
  }

  db_unprotect(PROTECT_READONLY);
  db_prepare(&s,
    "UPDATE subscriber"
    "   SET lastContact=now()/86400"
    " WHERE subscriberCode=hextoblob(%Q)"
    " RETURNING semail, date('now','+%d days');",
    zName, iInterval+1
  );
  rc = db_step(&s);
  if( rc==SQLITE_ROW ){
    @ <p>The email notification subscription for %h(db_column_text(&s,0))
    @ has been extended until %h(db_column_text(&s,1)) UTC.
  }else{
    @ <p>No such subscriber-id: %h(zName)</p>
  }
  db_finalize(&s);
  db_protect_pop();
  style_finish_page();
}


/* This is the message that gets sent to describe how to change
** or modify a subscription
*/
static const char zUnsubMsg[] =
@ To changes your subscription settings at %s visit this link:
@
@    %s/alerts/%s
@
@ To completely unsubscribe from %s, visit the following link:
@
@    %s/unsubscribe/%s
;

/*
** WEBPAGE: unsubscribe
** WEBPAGE: oneclickunsub
**
** Users visit this page to be delisted from email alerts.
**
** If a valid subscriber code is supplied in the name= query parameter,
** then that subscriber is delisted.
**
** Otherwise, If the users is logged in, then they are redirected
** to the /alerts page where they have an unsubscribe button.
**
** Non-logged-in users with no name= query parameter are invited to enter
** an email address to which will be sent the unsubscribe link that
** contains the correct subscriber code.
**
** The /unsubscribe page requires comfirmation.  The /oneclickunsub
** page unsubscribes immediately without any need to confirm.
*/
void unsubscribe_page(void){
  const char *zName = P("name");
  char *zErr = 0;
  int eErr = 0;
  unsigned int uSeed = 0;
  const char *zDecoded;
  char *zCaptcha = 0;
  int dx;
  int bSubmit;
  const char *zEAddr;
  char *zCode = 0;
  int sid = 0;

  if( zName==0 ) zName = P("scode");

  /* If a valid subscriber code is supplied, then either present the user
  ** with a confirmation, or if already confirmed, unsubscribe immediately.
  */
  if( zName
   && (sid = db_int(0, "SELECT subscriberId FROM subscriber"
                       " WHERE subscriberCode=hextoblob(%Q)", zName))!=0
  ){
    char *zUnsubName = mprintf("confirm%04x", sid);
    if( P(zUnsubName)!=0 ){
      alert_unsubscribe(sid, 1);
    }else if( sqlite3_strglob("*oneclick*",g.zPath)==0 ){
      alert_unsubscribe(sid, 0);
    }else if( P("manage")!=0 ){
      cgi_redirectf("%R/alerts/%s", zName);
    }else{
      style_header("Unsubscribe");
      form_begin(0, "%R/unsubscribe");
      @ <input type="hidden" name="scode" value="%h(zName)">
      @ <table border="0" cellpadding="10" width="100%%">
      @ <tr><td align="right">
      @ <input type="submit" name="%h(zUnsubName)" value="Unsubscribe">
      @ </td><td><big><b>&larr;</b></big></td>
      @ <td>Cancel your subscription to %h(g.zBaseURL) notifications
      @ </td><tr>
      @ <tr><td align="right">
      @ <input type="submit" name="manage" \
      @ value="Manage Subscription Settings">
      @ </td><td><big><b>&larr;</b></big></td>
      @ <td>Make other changes to your subscription preferences
      @ </td><tr>
      @ </table>
      @ </form>
      style_finish_page();
    }
    return;
  }

  /* Logged in users are redirected to the /alerts page */
  login_check_credentials();
  if( login_is_individual() ){
    cgi_redirectf("%R/alerts");
    return;
  }

  style_set_current_feature("alerts");

  zEAddr = PD("e","");
  dx = atoi(PD("dx","0"));
  bSubmit = P("submit")!=0 && P("e")!=0 && cgi_csrf_safe(2);
  if( bSubmit ){
    if( !captcha_is_correct(1) ){
      eErr = 2;
      zErr = mprintf("enter the security code shown below");
      bSubmit = 0;
    }
  }
  if( bSubmit ){
    zCode = db_text(0,"SELECT hex(subscriberCode) FROM subscriber"
                      " WHERE semail=%Q", zEAddr);
    if( zCode==0 ){
      eErr = 1;
      zErr = mprintf("not a valid email address");
      bSubmit = 0;
    }
  }
  if( bSubmit ){
    /* If we get this far, it means that a valid unsubscribe request has
    ** been submitted.  Send the appropriate email. */
    Blob hdr, body;
    AlertSender *pSender = alert_sender_new(0,0);
    blob_init(&hdr,0,0);
    blob_init(&body,0,0);
    blob_appendf(&hdr, "To: <%s>\r\n", zEAddr);
    blob_appendf(&hdr, "Subject: Unsubscribe Instructions\r\n");
    blob_appendf(&body, zUnsubMsg/*works-like:"%s%s%s%s%s%s"*/,
                  g.zBaseURL, g.zBaseURL, zCode, g.zBaseURL, g.zBaseURL, zCode);
    alert_send(pSender, &hdr, &body, 0);
    style_header("Unsubscribe Instructions Sent");
    if( pSender->zErr ){
      @ <h1>Internal Error</h1>
      @ <p>The following error was encountered while trying to send an
      @ email to %h(zEAddr):
      @ <blockquote><pre>
      @ %h(pSender->zErr)
      @ </pre></blockquote>
    }else{
      @ <p>An email has been sent to "%h(zEAddr)" that explains how to
      @ unsubscribe and/or modify your subscription settings</p>
    }
    alert_sender_free(pSender);
    style_finish_page();
    return;
  }

  /* Non-logged-in users have to enter an email address to which is
  ** sent a message containing the unsubscribe link.
  */
  style_header("Unsubscribe Request");
  @ <p>Fill out the form below to request an email message that will
  @ explain how to unsubscribe and/or change your subscription settings.</p>
  @
  form_begin(0, "%R/unsubscribe");
  @ <table class="subscribe">
  @ <tr>
  @  <td class="form_label">Email&nbsp;Address:</td>
  @  <td><input type="text" name="e" value="%h(zEAddr)" size="30"></td>
  if( eErr==1 ){
    @  <td><span class="loginError">&larr; %h(zErr)</span></td>
  }
  @ </tr>
  uSeed = captcha_seed();
  zDecoded = captcha_decode(uSeed, 0);
  zCaptcha = captcha_render(zDecoded);
  @ <tr>
  @  <td class="form_label">Security Code:</td>
  @  <td><input type="text" name="captcha" value="" size="30">
  captcha_speakit_button(uSeed, "Speak the code");
  @  <input type="hidden" name="captchaseed" value="%u(uSeed)"></td>
  if( eErr==2 ){
    @  <td><span class="loginError">&larr; %h(zErr)</span></td>
  }
  @ </tr>
  @ <tr>
  @  <td class="form_label">Options:</td>
  @  <td><label><input type="radio" name="dx" value="0" %s(dx?"":"checked")>\
  @  Modify subscription</label><br>
  @  <label><input type="radio" name="dx" value="1" %s(dx?"checked":"")>\
  @  Completely unsubscribe</label><br>
  @ <tr>
  @  <td></td>
  @  <td><input type="submit" name="submit" value="Submit"></td>
  @ </tr>
  @ </table>
  @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
  @ %h(zCaptcha)
  @ </pre>
  @ Enter the 8 characters above in the "Security Code" box<br/>
  @ </td></tr></table></div>
  @ </form>
  fossil_free(zErr);
  style_finish_page();
}

/*
** WEBPAGE: subscribers
**
** This page, accessible to administrators only,
** shows a list of subscriber email addresses.
** Clicking on an email takes one to the /alerts page
** for that email where the delivery settings can be
** modified.
*/
void subscriber_list_page(void){
  Blob sql;
  Stmt q;
  sqlite3_int64 iNow;
  int nTotal;
  int nPending;
  int nDel = 0;
  int iCutoff = db_get_int("email-renew-cutoff",0);
  int iWarning = db_get_int("email-renew-warning",0);
  char zCutoffClr[8];
  char zWarnClr[8];
  if( alert_webpages_disabled() ) return;
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  alert_submenu_common();
  style_submenu_element("Users","setup_ulist");
  style_set_current_feature("alerts");
  style_header("Subscriber List");
  nTotal = db_int(0, "SELECT count(*) FROM subscriber");
  nPending = db_int(0, "SELECT count(*) FROM subscriber WHERE NOT sverified");
  if( nPending>0 && P("purge") && cgi_csrf_safe(0) ){
    int nNewPending;
    db_multi_exec(
       "DELETE FROM subscriber"
       " WHERE NOT sverified AND mtime<now()-86400"
    );
    nNewPending = db_int(0, "SELECT count(*) FROM subscriber"
                            " WHERE NOT sverified");
    nDel = nPending - nNewPending;
    nPending = nNewPending;
    nTotal -= nDel;
  }
  if( nPending>0 ){
    @ <h1>%,d(nTotal) Subscribers, %,d(nPending) Pending</h1>
    if( nDel==0 && 0<db_int(0,"SELECT count(*) FROM subscriber"
            " WHERE NOT sverified AND mtime<now()-86400")
    ){
      style_submenu_element("Purge Pending","subscribers?purge");
    }
  }else{
    @ <h1>%,d(nTotal) Subscribers</h1>
  }
  if( nDel>0 ){
    @ <p>*** %d(nDel) pending subscriptions deleted ***</p>
  }
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
    "SELECT subscriberId,"                 /* 0 */
    "       semail,"                       /* 1 */
    "       ssub,"                         /* 2 */
    "       suname,"                       /* 3 */
    "       sverified,"                    /* 4 */
    "       sdigest,"                      /* 5 */
    "       mtime,"                        /* 6 */
    "       date(sctime,'unixepoch'),"     /* 7 */
    "       (SELECT uid FROM user WHERE login=subscriber.suname)," /* 8 */
    "       coalesce(lastContact,mtime/86400)"                     /* 9 */
    " FROM subscriber"
  );
  if( P("only")!=0 ){
    blob_append_sql(&sql, " WHERE ssub LIKE '%%%q%%'", P("only"));
    style_submenu_element("Show All","%R/subscribers");
  }
  blob_append_sql(&sql," ORDER BY mtime DESC");
  db_prepare_blob(&q, &sql);
  iNow = time(0);
  memcpy(zCutoffClr, hash_color("A"), sizeof(zCutoffClr));
  memcpy(zWarnClr, hash_color("HIJ"), sizeof(zWarnClr));
  @ <table border='1' class='sortable' \
  @ data-init-sort='6' data-column-types='tttttKKt'>
  @ <thead>
  @ <tr>
  @ <th>Email
  @ <th>Events
  @ <th>Digest-Only?
  @ <th>User
  @ <th>Verified?
  @ <th>Last change
  @ <th>Last contact
  @ <th>Created
  @ </tr>
  @ </thead><tbody>
  while( db_step(&q)==SQLITE_ROW ){
    sqlite3_int64 iMtime = db_column_int64(&q, 6);
    double rAge = (iNow - iMtime)/86400.0;
    int uid = db_column_int(&q, 8);
    const char *zUname = db_column_text(&q, 3);
    sqlite3_int64 iContact = db_column_int64(&q, 9);
    double rContact = (iNow/86400.0) - iContact;
    @ <tr>
    @ <td><a href='%R/alerts?sid=%d(db_column_int(&q,0))'>\
    @ %h(db_column_text(&q,1))</a></td>
    @ <td>%h(db_column_text(&q,2))</td>
    @ <td>%s(db_column_int(&q,5)?"digest":"")</td>
    if( uid ){
      @ <td><a href='%R/setup_uedit?id=%d(uid)'>%h(zUname)</a>
    }else{
      @ <td>%h(zUname)</td>
    }
    @ <td>%s(db_column_int(&q,4)?"yes":"pending")</td>
    @ <td data-sortkey='%010llx(iMtime)'>%z(human_readable_age(rAge))</td>
    @ <td data-sortkey='%010llx(iContact)'>\
    if( iContact>iWarning ){
      @ <span>\
    }else if( iContact>iCutoff ){
      @ <span style='background-color:%s(zWarnClr);'>\
    }else{
      @ <span style='background-color:%s(zCutoffClr);'>\
    }
    @ %z(human_readable_age(rContact))</td>
    @ <td>%h(db_column_text(&q,7))</td>
    @ </tr>
  }
  @ </tbody></table>
  db_finalize(&q);
  style_table_sorter();
  style_finish_page();
}

#if LOCAL_INTERFACE
/*
** A single event that might appear in an alert is recorded as an
** instance of the following object.
**
** type values:
**
**      c       A new check-in
**      f       An original forum post
**      n       New forum threads
**      r       Replies to my forum posts
**      x       An edit to a prior forum post
**      t       A new ticket or a change to an existing ticket
**      u       A user was added or received new permissions
**      w       A change to a wiki page
**      x       Edits to forum posts
*/
struct EmailEvent {
  int type;          /* 'c', 'f', 'n', 'r', 't', 'u', 'w', 'x' */
  int needMod;       /* Pending moderator approval */
  Blob hdr;          /* Header content, for forum entries */
  Blob txt;          /* Text description to appear in an alert */
  char *zFromName;   /* Human name of the sender */
  char *zPriors;     /* Upthread sender IDs for forum posts */
  EmailEvent *pNext; /* Next in chronological order */
};
#endif

/*
** Free a linked list of EmailEvent objects
*/
void alert_free_eventlist(EmailEvent *p){
  while( p ){
    EmailEvent *pNext = p->pNext;
    blob_reset(&p->txt);
    blob_reset(&p->hdr);
    fossil_free(p->zFromName);
    fossil_free(p->zPriors);
    fossil_free(p);
    p = pNext;
  }
}

/*
** Compute a string that is appropriate for the EmailEvent.zPriors field
** for a particular forum post.
**
** This string is an encode list of sender names and rids for all ancestors
** of the fpdi post - the post that fpid answer, the post that that parent
** post answers, and so forth back up to the root post. Duplicates sender
** names are omitted.
**
** The EmailEvent.zPriors field is used to screen events for people who
** only want to see replies to their own posts or to specific posts.
*/
static char *alert_compute_priors(int fpid){
  return db_text(0,
    "WITH priors(rid,who) AS ("
    "  SELECT firt, coalesce(euser,user)"
    "    FROM forumpost LEFT JOIN event ON fpid=objid"
    "   WHERE fpid=%d"
    "  UNION ALL"
    "  SELECT firt, coalesce(euser,user)"
    "    FROM priors, forumpost LEFT JOIN event ON fpid=objid"
    "   WHERE fpid=rid"
    ")"
    "SELECT ','||group_concat(DISTINCT 'u'||who)||"
           "','||group_concat(rid) FROM priors;",
    fpid
  );
}

/*
** Compute and return a linked list of EmailEvent objects
** corresponding to the current content of the temp.wantalert
** table which should be defined as follows:
**
**     CREATE TEMP TABLE wantalert(eventId TEXT, needMod BOOLEAN);
*/
EmailEvent *alert_compute_event_text(int *pnEvent, int doDigest){
  Stmt q;
  EmailEvent *p;
  EmailEvent anchor;
  EmailEvent *pLast;
  const char *zUrl = db_get("email-url","http://localhost:8080");
  const char *zFrom;
  const char *zSub;


  /* First do non-forum post events */
  db_prepare(&q,
    "SELECT"
    " CASE WHEN event.type='t'"
         " THEN (SELECT substr(tagname,5) FROM tag"
                " WHERE tagid=event.tagid AND tagname LIKE 'tkt-%%')"
         " ELSE blob.uuid END,"  /* 0 */
    " datetime(event.mtime),"    /* 1 */
    " coalesce(ecomment,comment)"
    "  || ' (user: ' || coalesce(euser,user,'?')"
    "  || (SELECT case when length(x)>0 then ' tags: ' || x else '' end"
    "      FROM (SELECT group_concat(substr(tagname,5), ', ') AS x"
    "              FROM tag, tagxref"
    "             WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
    "               AND tagxref.rid=blob.rid AND tagxref.tagtype>0))"
    "  || ')' as comment,"       /* 2 */
    " wantalert.eventId,"        /* 3 */
    " wantalert.needMod"         /* 4 */
    " FROM temp.wantalert, event, blob"
    " WHERE blob.rid=event.objid"
    "   AND event.objid=substr(wantalert.eventId,2)+0"
    "   AND (%d OR eventId NOT GLOB 'f*')"
    " ORDER BY event.mtime",
    doDigest
  );
  memset(&anchor, 0, sizeof(anchor));
  pLast = &anchor;
  *pnEvent = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = "";
    const char *zComment = db_column_text(&q, 2);
    p = fossil_malloc_zero( sizeof(EmailEvent) );
    pLast->pNext = p;
    pLast = p;
    p->type = db_column_text(&q, 3)[0];
    p->needMod = db_column_int(&q, 4);
    p->zFromName = 0;
    p->pNext = 0;
    switch( p->type ){
      case 'c':  zType = "Check-In";        break;
      /* case 'f':  -- forum posts omitted from this loop.  See below */
      case 't':  zType = "Ticket Change";   break;
      case 'w': {
        zType = "Wiki Edit";
        switch( zComment ? *zComment : 0 ){
          case ':': ++zComment; break;
          case '+': zType = "Wiki Added"; ++zComment; break;
          case '-': zType = "Wiki Removed"; ++zComment; break;
        }
        break;
      }
    }
    blob_init(&p->hdr, 0, 0);
    blob_init(&p->txt, 0, 0);
    blob_appendf(&p->txt,"== %s %s ==\n%s\n%s/info/%.20s\n",
      db_column_text(&q,1),
      zType,
      zComment,
      zUrl,
      db_column_text(&q,0)
    );
    if( p->needMod ){
      blob_appendf(&p->txt,
        "** Pending moderator approval (%s/modreq) **\n",
        zUrl
      );
    }
    (*pnEvent)++;
  }
  db_finalize(&q);

  /* Early-out if forumpost is not a table in this repository */
  if( !db_table_exists("repository","forumpost") ){
    return anchor.pNext;
  }

  /* For digests, the previous loop also handled forumposts already */
  if( doDigest ){
    return anchor.pNext;
  }

  /* If we reach this point, it means that forumposts exist and this
  ** is a normal email alert.  Construct full-text forum post alerts
  ** using a format that enables them to be sent as separate emails.
  */
  db_prepare(&q,
    "SELECT"
    " forumpost.fpid,"                                     /* 0: fpid */
    " (SELECT uuid FROM blob WHERE rid=forumpost.fpid),"   /* 1: hash */
    " datetime(event.mtime),"                              /* 2: date/time */
    " substr(comment,instr(comment,':')+2),"               /* 3: comment */
    " (WITH thread(fpid,fprev) AS ("
    "    SELECT fpid,fprev FROM forumpost AS tx"
    "     WHERE tx.froot=forumpost.froot),"
    "  basepid(fpid,bpid) AS ("
    "    SELECT fpid, fpid FROM thread WHERE fprev IS NULL"
    "    UNION ALL"
    "    SELECT thread.fpid, basepid.bpid FROM  basepid, thread"
    "     WHERE basepid.fpid=thread.fprev)"
    "  SELECT uuid FROM blob, basepid"
    "   WHERE basepid.fpid=forumpost.firt"
    "     AND blob.rid=basepid.bpid),"                     /* 4: in-reply-to */
    " wantalert.needMod,"                                  /* 5: moderated */
    " coalesce(display_name(info),euser,user),"            /* 6: user */
    " forumpost.fprev IS NULL"                             /* 7: is an edit */
    " FROM temp.wantalert, event, forumpost"
    "      LEFT JOIN user ON (login=coalesce(euser,user))"
    " WHERE event.objid=substr(wantalert.eventId,2)+0"
    "   AND eventId GLOB 'f*'"
    "   AND forumpost.fpid=event.objid"
    " ORDER BY event.mtime"
  );
  zFrom = db_get("email-self",0);
  zSub = db_get("email-subname","");
  while( db_step(&q)==SQLITE_ROW ){
    int fpid = db_column_int(&q,0);
    Manifest *pPost = manifest_get(fpid, CFTYPE_FORUM, 0);
    const char *zIrt;
    const char *zUuid;
    const char *zTitle;
    const char *z;
    if( pPost==0 ) continue;
    p = fossil_malloc( sizeof(EmailEvent) );
    pLast->pNext = p;
    pLast = p;
    p->type = db_column_int(&q,7) ? 'f' : 'x';
    p->needMod = db_column_int(&q, 5);
    z = db_column_text(&q,6);
    p->zFromName = z && z[0] ? fossil_strdup(z) : 0;
    p->zPriors = alert_compute_priors(fpid);
    p->pNext = 0;
    blob_init(&p->hdr, 0, 0);
    zUuid = db_column_text(&q, 1);
    zTitle = db_column_text(&q, 3);
    if( p->needMod ){
      blob_appendf(&p->hdr, "Subject: %s Pending Moderation: %s\r\n",
                   zSub, zTitle);
    }else{
      blob_appendf(&p->hdr, "Subject: %s %s\r\n", zSub, zTitle);
      blob_appendf(&p->hdr, "Message-Id: <%.32s@%s>\r\n",
                   zUuid, alert_hostname(zFrom));
      zIrt = db_column_text(&q, 4);
      if( zIrt && zIrt[0] ){
        blob_appendf(&p->hdr, "In-Reply-To: <%.32s@%s>\r\n",
                     zIrt, alert_hostname(zFrom));
      }
    }
    blob_init(&p->txt, 0, 0);
    if( p->needMod ){
      blob_appendf(&p->txt,
        "** Pending moderator approval (%s/modreq) **\n",
        zUrl
      );
    }
    blob_appendf(&p->txt,
      "Forum post by %s on %s\n",
      pPost->zUser, db_column_text(&q, 2));
    blob_appendf(&p->txt, "%s/forumpost/%S\n\n", zUrl, zUuid);
    blob_append(&p->txt, pPost->zWiki, -1);
    manifest_destroy(pPost);
    (*pnEvent)++;
  }
  db_finalize(&q);

  return anchor.pNext;
}

/*
** Put a header on an alert email
*/
void email_header(Blob *pOut){
  blob_appendf(pOut,
    "This is an automated email reporting changes "
    "on Fossil repository %s (%s/timeline)\n",
    db_get("email-subname","(unknown)"),
    db_get("email-url","http://localhost:8080"));
}

/*
** COMMAND:  test-alert
**
** Usage: %fossil test-alert EVENTID ...
**
** Generate the text of an email alert for all of the EVENTIDs
** listed on the command-line.  Or if no events are listed on the
** command line, generate text for all events named in the
** pending_alert table.  The text of the email alerts appears on
** standard output.
**
** This command is intended for testing and debugging Fossil itself,
** for example when enhancing the email alert system or fixing bugs
** in the email alert system.  If you are not making changes to the
** Fossil source code, this command is probably not useful to you.
**
** EVENTIDs are text.  The first character is 'c', 'f', 't', or 'w'
** for check-in, forum, ticket, or wiki.  The remaining text is a
** integer that references the EVENT.OBJID value for the event.
** Run /timeline?showid to see these OBJID values.
**
** Options:
**      --digest           Generate digest alert text
**      --needmod          Assume all events are pending moderator approval
*/
void test_alert_cmd(void){
  Blob out;
  int nEvent;
  int needMod;
  int doDigest;
  EmailEvent *pEvent, *p;

  doDigest = find_option("digest",0,0)!=0;
  needMod = find_option("needmod",0,0)!=0;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  db_begin_transaction();
  alert_schema(0);
  db_multi_exec("CREATE TEMP TABLE wantalert(eventid TEXT, needMod BOOLEAN)");
  if( g.argc==2 ){
    db_multi_exec(
      "INSERT INTO wantalert(eventId,needMod)"
      " SELECT eventid, %d FROM pending_alert", needMod);
  }else{
    int i;
    for(i=2; i<g.argc; i++){
      db_multi_exec("INSERT INTO wantalert(eventId,needMod) VALUES(%Q,%d)",
           g.argv[i], needMod);
    }
  }
  blob_init(&out, 0, 0);
  email_header(&out);
  pEvent = alert_compute_event_text(&nEvent, doDigest);
  for(p=pEvent; p; p=p->pNext){
    blob_append(&out, "\n", 1);
    if( blob_size(&p->hdr) ){
      blob_append(&out, blob_buffer(&p->hdr), blob_size(&p->hdr));
      blob_append(&out, "\n", 1);
    }
    blob_append(&out, blob_buffer(&p->txt), blob_size(&p->txt));
  }
  alert_free_eventlist(pEvent);
  fossil_print("%s", blob_str(&out));
  blob_reset(&out);
  db_end_transaction(0);
}

/*
** COMMAND:  test-add-alerts
**
** Usage: %fossil test-add-alerts [OPTIONS] EVENTID ...
**
** Add one or more events to the pending_alert queue.  Use this
** command during testing to force email notifications for specific
** events.
**
** EVENTIDs are text.  The first character is 'c', 'f', 't', or 'w'
** for check-in, forum, ticket, or wiki.  The remaining text is a
** integer that references the EVENT.OBJID value for the event.
** Run /timeline?showid to see these OBJID values.
**
** Options:
**    --backoffice        Run alert_backoffice() after all alerts have
**                        been added.  This will cause the alerts to be
**                        sent out with the SENDALERT_TRACE option.
**    --debug             Like --backoffice, but add the SENDALERT_STDOUT
**                        so that emails are printed to standard output
**                        rather than being sent.
**    --digest            Process emails using SENDALERT_DIGEST
*/
void test_add_alert_cmd(void){
  int i;
  int doAuto = find_option("backoffice",0,0)!=0;
  unsigned mFlags = 0;
  if( find_option("debug",0,0)!=0 ){
    doAuto = 1;
    mFlags = SENDALERT_STDOUT;
  }
  if( find_option("digest",0,0)!=0 ){
    mFlags |= SENDALERT_DIGEST;
  }
  db_find_and_open_repository(0, 0);
  verify_all_options();
  db_begin_write();
  alert_schema(0);
  for(i=2; i<g.argc; i++){
    db_multi_exec("REPLACE INTO pending_alert(eventId) VALUES(%Q)", g.argv[i]);
  }
  db_end_transaction(0);
  if( doAuto ){
    alert_backoffice(SENDALERT_TRACE|mFlags);
  }
}

/*
** Minimum number of days between renewal messages
*/
#define ALERT_RENEWAL_MSG_FREQUENCY  7   /* Do renewals at most once/week */

/*
** Construct the header and body for an email message that will alert
** a subscriber that their subscriptions are about to expire.
*/
static void alert_renewal_msg(
  Blob *pHdr,            /* Write email header here */
  Blob *pBody,           /* Write email body here */
  const char *zCode,     /* The subscriber code */
  int lastContact,       /* Last contact (days since 1970) */
  const char *zEAddr,    /* Subscriber email address.  Send to this. */
  const char *zSub,      /* Subscription codes */
  const char *zRepoName, /* Name of the sending Fossil repostory */
  const char *zUrl       /* URL for the sending Fossil repostory */
){
  blob_appendf(pHdr,"To: <%s>\r\n", zEAddr);
  blob_appendf(pHdr,"Subject: %s Subscription to %s expires soon\r\n",
    zRepoName, zUrl);
  blob_appendf(pBody,
    "\nTo renew your subscription, click the following link:\n"
    "\n  %s/renew/%s\n\n",
    zUrl, zCode
  );
  blob_appendf(pBody,
    "You are currently receiving email notification for the following events\n"
    "on the %s Fossil repository at %s:\n\n",
    zRepoName, zUrl
  );
  if( strchr(zSub, 'a') )  blob_appendf(pBody, "  *  Announcements\n");
  if( strchr(zSub, 'c') )  blob_appendf(pBody, "  *  Check-ins\n");
  if( strchr(zSub, 'f') )  blob_appendf(pBody, "  *  Forum posts\n");
  if( strchr(zSub, 't') )  blob_appendf(pBody, "  *  Ticket changes\n");
  if( strchr(zSub, 'u') )  blob_appendf(pBody, "  *  User permission elevation\n");
  if( strchr(zSub, 'w') )  blob_appendf(pBody, "  *  Wiki changes\n");
  blob_appendf(pBody, "\n"
    "If you take no action, your subscription will expire and you will be\n"
    "unsubscribed in about %d days.  To make other changes or to unsubscribe\n"
    "immediately, visit the following webpage:\n\n"
    "  %s/alerts/%s\n\n",
    ALERT_RENEWAL_MSG_FREQUENCY, zUrl, zCode
  );
}

/*
** If zUser is a sender of one of the ancestors of a forum post
** (if zUser appears in zPriors) then return true.
*/
static int alert_in_priors(const char *zUser, const char *zPriors){
  int n = (int)strlen(zUser);
  char zBuf[200];
  if( n>195 ) return 0;
  if( zPriors==0 || zPriors[0]==0 ) return 0;
  zBuf[0] = ',';
  zBuf[1] = 'u';
  memcpy(zBuf+2, zUser, n+1);
  return strstr(zPriors, zBuf)!=0;
}

#if INTERFACE
/*
** Flags for alert_send_alerts()
*/
#define SENDALERT_DIGEST      0x0001    /* Send a digest */
#define SENDALERT_PRESERVE    0x0002    /* Do not mark the task as done */
#define SENDALERT_STDOUT      0x0004    /* Print emails instead of sending */
#define SENDALERT_TRACE       0x0008    /* Trace operation for debugging */
#define SENDALERT_RENEWAL     0x0010    /* Send renewal notices */

#endif /* INTERFACE */

/*
** Send alert emails to subscribers.
**
** This procedure is run by either the backoffice, or in response to the
** "fossil alerts send" command.  Details of operation are controlled by
** the flags parameter.
**
** Here is a summary of what happens:
**
**   (1) Create a TEMP table wantalert(eventId,needMod) and fill it with
**       all the events that we want to send alerts about.  The needMod
**       flags is set if and only if the event is still awaiting
**       moderator approval.  Events with the needMod flag are only
**       shown to users that have moderator privileges.
**
**   (2) Call alert_compute_event_text() to compute a list of EmailEvent
**       objects that describe all events about which we want to send
**       alerts.
**
**   (3) Loop over all subscribers.  Compose and send one or more email
**       messages to each subscriber that describe the events for
**       which the subscriber has expressed interest and has
**       appropriate privileges.
**
**   (4) Update the pending_alerts table to indicate that alerts have been
**       sent.
**
** Update 2018-08-09:  Do step (3) before step (4).  Update the
** pending_alerts table *before* the emails are sent.  That way, if
** the process malfunctions or crashes, some notifications may never
** be sent.  But that is better than some recurring bug causing
** subscribers to be flooded with repeated notifications every 60
** seconds!
*/
int alert_send_alerts(u32 flags){
  EmailEvent *pEvents, *p;
  int nEvent = 0;
  int nSent = 0;
  Stmt q;
  const char *zDigest = "false";
  Blob hdr, body;
  const char *zUrl;
  const char *zRepoName;
  const char *zFrom;
  const char *zDest = (flags & SENDALERT_STDOUT) ? "stdout" : 0;
  AlertSender *pSender = 0;
  u32 senderFlags = 0;
  int iInterval = 0;              /* Subscription renewal interval */

  if( g.fSqlTrace ) fossil_trace("-- BEGIN alert_send_alerts(%u)\n", flags);
  alert_schema(0);
  if( !alert_enabled() && (flags & SENDALERT_STDOUT)==0 ) goto send_alert_done;
  zUrl = db_get("email-url",0);
  if( zUrl==0 ) goto send_alert_done;
  zRepoName = db_get("email-subname",0);
  if( zRepoName==0 ) goto send_alert_done;
  zFrom = db_get("email-self",0);
  if( zFrom==0 ) goto send_alert_done;
  if( flags & SENDALERT_TRACE ){
    senderFlags |= ALERT_TRACE;
  }
  pSender = alert_sender_new(zDest, senderFlags);

  /* Step (1):  Compute the alerts that need sending
  */
  db_multi_exec(
    "DROP TABLE IF EXISTS temp.wantalert;"
    "CREATE TEMP TABLE wantalert(eventId TEXT, needMod BOOLEAN, sentMod);"
  );
  if( flags & SENDALERT_DIGEST ){
    /* Unmoderated changes are never sent as part of a digest */
    db_multi_exec(
      "INSERT INTO wantalert(eventId,needMod)"
      " SELECT eventid, 0"
      "   FROM pending_alert"
      "  WHERE sentDigest IS FALSE"
      "    AND NOT EXISTS(SELECT 1 FROM private WHERE rid=substr(eventid,2));"
    );
    zDigest = "true";
  }else{
    /* Immediate alerts might include events that are subject to
    ** moderator approval */
    db_multi_exec(
      "INSERT INTO wantalert(eventId,needMod,sentMod)"
      " SELECT eventid,"
      "        EXISTS(SELECT 1 FROM private WHERE rid=substr(eventid,2)),"
      "        sentMod"
      "   FROM pending_alert"
      "  WHERE sentSep IS FALSE;"
      "DELETE FROM wantalert WHERE needMod AND sentMod;"
    );
  }
  if( g.fSqlTrace ){
    fossil_trace("-- wantalert contains %d rows\n",
        db_int(0, "SELECT count(*) FROM wantalert")
    );
  }

  /* Step 2: compute EmailEvent objects for every notification that
  ** needs sending.
  */
  pEvents = alert_compute_event_text(&nEvent, (flags & SENDALERT_DIGEST)!=0);
  if( nEvent==0 ) goto send_alert_expiration_warnings;

  /* Step 4a: Update the pending_alerts table to designate the
  ** alerts as having all been sent.  This is done *before* step (3)
  ** so that a crash will not cause alerts to be sent multiple times.
  ** Better a missed alert than being spammed with hundreds of alerts
  ** due to a bug.
  */
  if( (flags & SENDALERT_PRESERVE)==0 ){
    if( flags & SENDALERT_DIGEST ){
      db_multi_exec(
        "UPDATE pending_alert SET sentDigest=true"
        " WHERE eventid IN (SELECT eventid FROM wantalert);"
      );
    }else{
      db_multi_exec(
        "UPDATE pending_alert SET sentSep=true"
        " WHERE eventid IN (SELECT eventid FROM wantalert WHERE NOT needMod);"
        "UPDATE pending_alert SET sentMod=true"
        " WHERE eventid IN (SELECT eventid FROM wantalert WHERE needMod);"
      );
    }
  }

  /* Step 3: Loop over subscribers.  Send alerts
  */
  blob_init(&hdr, 0, 0);
  blob_init(&body, 0, 0);
  db_prepare(&q,
     "SELECT"
     " hex(subscriberCode),"  /* 0 */
     " semail,"               /* 1 */
     " ssub,"                 /* 2 */
     " fullcap(user.cap),"    /* 3 */
     " suname"                /* 4 */
     " FROM subscriber LEFT JOIN user ON (login=suname)"
     " WHERE sverified"
     "   AND NOT sdonotcall"
     "   AND sdigest IS %s"
     "   AND coalesce(subscriber.lastContact*86400,subscriber.mtime)>=%d",
     zDigest/*safe-for-%s*/,
     db_get_int("email-renew-cutoff",0)
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zCode = db_column_text(&q, 0);
    const char *zSub = db_column_text(&q, 2);
    const char *zEmail = db_column_text(&q, 1);
    const char *zCap = db_column_text(&q, 3);
    const char *zUser = db_column_text(&q, 4);
    int nHit = 0;
    for(p=pEvents; p; p=p->pNext){
      if( strchr(zSub,p->type)==0 ){
        if( p->type!='f' ) continue;
        if( strchr(zSub,'n')!=0 && (p->zPriors==0 || p->zPriors[0]==0) ){
          /* New post: accepted */
        }else if( strchr(zSub,'r')!=0 && zUser!=0
               && alert_in_priors(zUser, p->zPriors) ){
          /* A follow-up to a post written by the user: accept */
        }else{
          continue;
        }
      }
      if( p->needMod ){
        /* For events that require moderator approval, only send an alert
        ** if the recipient is a moderator for that type of event.  Setup
        ** and Admin users always get notified. */
        char xType = '*';
        if( strpbrk(zCap,"as")==0 ){
          switch( p->type ){
            case 'x': case 'f':
            case 'n': case 'r':  xType = '5';  break;
            case 't':            xType = 'q';  break;
            case 'w':            xType = 'l';  break;
            /* Note: case 'u' is not handled here */
          }
          if( strchr(zCap,xType)==0 ) continue;
        }
      }else if( strchr(zCap,'s')!=0 || strchr(zCap,'a')!=0 ){
        /* Setup and admin users can get any notification that does not
        ** require moderation */
      }else{
        /* Other users only see the alert if they have sufficient
        ** privilege to view the event itself */
        char xType = '*';
        switch( p->type ){
          case 'c':            xType = 'o';  break;
          case 'x': case 'f':
          case 'n': case 'r':  xType = '2';  break;
          case 't':            xType = 'r';  break;
          case 'w':            xType = 'j';  break;
          /* Note: case 'u' is not handled here */
        }
        if( strchr(zCap,xType)==0 ) continue;
      }
      if( blob_size(&p->hdr)>0 ){
        /* This alert should be sent as a separate email */
        Blob fhdr, fbody;
        blob_init(&fhdr, 0, 0);
        blob_appendf(&fhdr, "To: <%s>\r\n", zEmail);
        blob_append(&fhdr, blob_buffer(&p->hdr), blob_size(&p->hdr));
        blob_init(&fbody, blob_buffer(&p->txt), blob_size(&p->txt));
        if( pSender->zListId  && pSender->zListId[0] ){
           blob_appendf(&fhdr, "List-Id: %s\r\n", pSender->zListId);
           blob_appendf(&fhdr, "List-Unsubscribe: <%s/oneclickunsub/%s>\r\n",
                        zUrl, zCode);
           blob_appendf(&fhdr,
                       "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n");
           blob_appendf(&fbody, "\n-- \nUnsubscribe: %s/unsubscribe/%s\n",
                        zUrl, zCode);
           /* blob_appendf(&fbody, "Subscription settings: %s/alerts/%s\n",
           **   zUrl, zCode); */
        }
        alert_send(pSender,&fhdr,&fbody,p->zFromName);
        nSent++;
        blob_reset(&fhdr);
        blob_reset(&fbody);
      }else{
        /* Events other than forum posts are gathered together into
        ** a single email message */
        if( nHit==0 ){
          blob_appendf(&hdr,"To: <%s>\r\n", zEmail);
          blob_appendf(&hdr,"Subject: %s activity alert\r\n", zRepoName);
          blob_appendf(&body,
            "This is an automated email sent by the Fossil repository "
            "at %s to report changes.\n",
            zUrl
          );
        }
        nHit++;
        blob_append(&body, "\n", 1);
        blob_append(&body, blob_buffer(&p->txt), blob_size(&p->txt));
      }
    }
    if( nHit==0 ) continue;
    if( pSender->zListId  && pSender->zListId[0] ){
      blob_appendf(&hdr, "List-Id: %s\r\n", pSender->zListId);
      blob_appendf(&hdr, "List-Unsubscribe: <%s/oneclickunsub/%s>\r\n",
           zUrl, zCode);
      blob_appendf(&hdr,
            "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n");
      blob_appendf(&body,"\n-- \nSubscription info: %s/alerts/%s\n",
           zUrl, zCode);
    }
    alert_send(pSender,&hdr,&body,0);
    nSent++;
    blob_truncate(&hdr, 0);
    blob_truncate(&body, 0);
  }
  blob_reset(&hdr);
  blob_reset(&body);
  db_finalize(&q);
  alert_free_eventlist(pEvents);

  /* Step 4b: Update the pending_alerts table to remove all of the
  ** alerts that have been completely sent.
  */
  db_multi_exec("DELETE FROM pending_alert WHERE sentDigest AND sentSep;");

  /* Send renewal messages to subscribers whose subscriptions are about
  ** to expire.  Only do this if:
  **
  **  (1)  email-renew-interval is 14 or greater (or in other words if
  **       subscription expiration is enabled).
  **
  **  (2)  The SENDALERT_RENEWAL flag is set
  */
send_alert_expiration_warnings:
  if( (flags & SENDALERT_RENEWAL)!=0
   && (iInterval = db_get_int("email-renew-interval",0))>=14
  ){
    int iNow = (int)(time(0)/86400);
    int iOldWarn = db_get_int("email-renew-warning",0);
    int iNewWarn = iNow - iInterval + ALERT_RENEWAL_MSG_FREQUENCY;
    if( iNewWarn >= iOldWarn + ALERT_RENEWAL_MSG_FREQUENCY ){
      db_prepare(&q,
         "SELECT"
         "  hex(subscriberCode),"     /* 0 */
         "  lastContact,"             /* 1 */
         "  semail,"                  /* 2 */
         "  ssub"                     /* 3 */
         " FROM subscriber"
         " WHERE lastContact<=%d AND lastContact>%d"
         "   AND NOT sdonotcall"
         "   AND length(sdigest)>0",
         iNewWarn, iOldWarn
      );
      while( db_step(&q)==SQLITE_ROW ){
        Blob hdr, body;
        const char *zCode = db_column_text(&q,0);
        blob_init(&hdr, 0, 0);
        blob_init(&body, 0, 0);
        alert_renewal_msg(&hdr, &body,
           zCode,
           db_column_int(&q,1),
           db_column_text(&q,2),
           db_column_text(&q,3),
           zRepoName, zUrl);
        if( pSender->zListId  && pSender->zListId[0] ){
           blob_appendf(&hdr, "List-Id: %s\r\n", pSender->zListId);
           blob_appendf(&hdr, "List-Unsubscribe: <%s/oneclickunsub/%s>\r\n",
                        zUrl, zCode);
           blob_appendf(&hdr,
                       "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n");
           blob_appendf(&body, "\n-- \nUnsubscribe: %s/unsubscribe/%s\n",
                        zUrl, zCode);
        }
        alert_send(pSender,&hdr,&body,0);
        blob_reset(&hdr);
        blob_reset(&body);
      }
      db_finalize(&q);
      if( (flags & SENDALERT_PRESERVE)==0 ){
        if( iOldWarn>0 ){
          db_set_int("email-renew-cutoff", iOldWarn, 0);
        }
        db_set_int("email-renew-warning", iNewWarn, 0);
      }
    }
  }

send_alert_done:
  alert_sender_free(pSender);
  if( g.fSqlTrace ) fossil_trace("-- END alert_send_alerts(%u)\n", flags);
  return nSent;
}

/*
** Do backoffice processing for email notifications.  In other words,
** check to see if any email notifications need to occur, and then
** do them.
**
** This routine is intended to run in the background, after webpages.
**
** The mFlags option is zero or more of the SENDALERT_* flags.  Normally
** this flag is zero, but the test-set-alert command sets it to
** SENDALERT_TRACE.
*/
int alert_backoffice(u32 mFlags){
  int iJulianDay;
  int nSent = 0;
  if( !alert_tables_exist() ) return 0;
  nSent = alert_send_alerts(mFlags);
  iJulianDay = db_int(0, "SELECT julianday('now')");
  if( iJulianDay>db_get_int("email-last-digest",0) ){
    db_set_int("email-last-digest",iJulianDay,0);
    nSent += alert_send_alerts(SENDALERT_DIGEST|SENDALERT_RENEWAL|mFlags);
  }
  return nSent;
}

/*
** WEBPAGE: contact_admin
**
** A web-form to send an email message to the repository administrator,
** or (with appropriate permissions) to anybody.
*/
void contact_admin_page(void){
  const char *zAdminEmail = db_get("email-admin",0);
  unsigned int uSeed = 0;
  const char *zDecoded;
  char *zCaptcha = 0;

  login_check_credentials();
  style_set_current_feature("alerts");
  if( zAdminEmail==0 || zAdminEmail[0]==0 ){
    style_header("Outbound Email Disabled");
    @ <p>Outbound email is disabled on this repository
    style_finish_page();
    return;
  }
  if( P("submit")!=0
   && P("subject")!=0
   && P("msg")!=0
   && P("from")!=0
   && cgi_csrf_safe(2)
   && captcha_is_correct(0)
  ){
    Blob hdr, body;
    AlertSender *pSender = alert_sender_new(0,0);
    blob_init(&hdr, 0, 0);
    blob_appendf(&hdr, "To: <%s>\r\nSubject: %s administrator message\r\n",
                 zAdminEmail, db_get("email-subname","Fossil Repo"));
    blob_init(&body, 0, 0);
    blob_appendf(&body, "Message from [%s]\n", PT("from")/*safe-for-%s*/);
    blob_appendf(&body, "Subject: [%s]\n\n", PT("subject")/*safe-for-%s*/);
    blob_appendf(&body, "%s", PT("msg")/*safe-for-%s*/);
    alert_send(pSender, &hdr, &body, 0);
    style_header("Message Sent");
    if( pSender->zErr ){
      @ <h1>Internal Error</h1>
      @ <p>The following error was reported by the system:
      @ <blockquote><pre>
      @ %h(pSender->zErr)
      @ </pre></blockquote>
    }else{
      @ <p>Your message has been sent to the repository administrator.
      @ Thank you for your input.</p>
    }
    alert_sender_free(pSender);
    style_finish_page();
    return;
  }
  if( captcha_needed() ){
    uSeed = captcha_seed();
    zDecoded = captcha_decode(uSeed, 0);
    zCaptcha = captcha_render(zDecoded);
  }
  style_set_current_feature("alerts");
  style_header("Message To Administrator");
  form_begin(0, "%R/contact_admin");
  @ <p>Enter a message to the repository administrator below:</p>
  @ <table class="subscribe">
  if( zCaptcha ){
    @ <tr>
    @  <td class="form_label">Security&nbsp;Code:</td>
    @  <td><input type="text" name="captcha" value="" size="10">
    captcha_speakit_button(uSeed, "Speak the code");
    @  <input type="hidden" name="captchaseed" value="%u(uSeed)"></td>
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Your&nbsp;Email&nbsp;Address:</td>
  @  <td><input type="text" name="from" value="%h(PT("from"))" size="30"></td>
  @ </tr>
  @ <tr>
  @  <td class="form_label">Subject:</td>
  @  <td><input type="text" name="subject" value="%h(PT("subject"))"\
  @  size="80"></td>
  @ </tr>
  @ <tr>
  @  <td class="form_label">Message:</td>
  @  <td><textarea name="msg" cols="80" rows="10" wrap="virtual">\
  @ %h(PT("msg"))</textarea>
  @ </tr>
  @ <tr>
  @   <td></td>
  @   <td><input type="submit" name="submit" value="Send Message">
  @ </tr>
  @ </table>
  if( zCaptcha ){
    @ <div class="captcha"><table class="captcha"><tr><td><pre class="captcha">
    @ %h(zCaptcha)
    @ </pre>
    @ Enter the 8 characters above in the "Security Code" box<br/>
    @ </td></tr></table></div>
  }
  @ </form>
  style_finish_page();
}

/*
** Send an annoucement message described by query parameter.
** Permission to do this has already been verified.
*/
static char *alert_send_announcement(void){
  AlertSender *pSender;
  char *zErr;
  const char *zTo = PT("to");
  char *zSubject = PT("subject");
  int bAll = PB("all");
  int bAA = PB("aa");
  int bMods = PB("mods");
  const char *zSub = db_get("email-subname", "[Fossil Repo]");
  const char *zName = P("name");    /* Debugging options */
  const char *zDest = 0;            /* How to send the announcement */
  int bTest = 0;
  Blob hdr, body;

  if( fossil_strcmp(zName, "test2")==0 ){
    bTest = 2;
    zDest = "blob";
  }else if( fossil_strcmp(zName, "test3")==0 ){
    bTest = 3;
    if( fossil_strcmp(db_get("email-send-method",""),"relay")==0 ){
      zDest = "debug-relay";
    }
  }
  blob_init(&body, 0, 0);
  blob_init(&hdr, 0, 0);
  blob_appendf(&body, "%s", PT("msg")/*safe-for-%s*/);
  pSender = alert_sender_new(zDest, 0);
  if( zTo[0] ){
    blob_appendf(&hdr, "To: <%s>\r\nSubject: %s %s\r\n", zTo, zSub, zSubject);
    alert_send(pSender, &hdr, &body, 0);
  }
  if( bAll || bAA || bMods ){
    Stmt q;
    int nUsed = blob_size(&body);
    const char *zURL =  db_get("email-url",0);
    if( bAll ){
      db_prepare(&q, "SELECT semail, hex(subscriberCode) FROM subscriber "
                     " WHERE sverified AND NOT sdonotcall");
    }else if( bAA ){
      db_prepare(&q, "SELECT semail, hex(subscriberCode) FROM subscriber "
                     " WHERE sverified AND NOT sdonotcall"
                     " AND ssub LIKE '%%a%%'");
    }else if( bMods ){
      db_prepare(&q,
        "SELECT semail, hex(subscriberCode)"
        "  FROM subscriber, user "
        " WHERE sverified AND NOT sdonotcall"
        "   AND suname=login"
        "   AND fullcap(cap) GLOB '*5*'");
    }
    while( db_step(&q)==SQLITE_ROW ){
      const char *zCode = db_column_text(&q, 1);
      zTo = db_column_text(&q, 0);
      blob_truncate(&hdr, 0);
      blob_appendf(&hdr, "To: <%s>\r\nSubject: %s %s\r\n", zTo, zSub, zSubject);
      if( zURL ){
        blob_truncate(&body, nUsed);
        blob_appendf(&body,"\n-- \nSubscription info: %s/alerts/%s\n",
           zURL, zCode);
      }
      alert_send(pSender, &hdr, &body, 0);
    }
    db_finalize(&q);
  }
  if( bTest && blob_size(&pSender->out) ){
    /* If the URL is "/announce/test2" then no email is actually sent.
    ** Instead, the text of the email that would have been sent is
    ** displayed in the result window.
    **
    ** If the URL is "/announce/test3" and the email-send-method is "relay"
    ** then the announcement is sent as it normally would be, but a
    ** transcript of the SMTP conversation with the MTA is shown here.
    */
    blob_trim(&pSender->out);
    @ <pre style='border: 2px solid blue; padding: 1ex;'>
    @ %h(blob_str(&pSender->out))
    @ </pre>
    blob_reset(&pSender->out);
  }
  zErr = pSender->zErr;
  pSender->zErr = 0;
  alert_sender_free(pSender);
  return zErr;
}


/*
** WEBPAGE: announce
**
** A web-form, available to users with the "Send-Announcement" or "A"
** capability, that allows one to send announcements to whomever
** has subscribed to receive announcements.  The administrator can
** also send a message to an arbitrary email address and/or to all
** subscribers regardless of whether or not they have elected to
** receive announcements.
*/
void announce_page(void){
  const char *zAction = "announce";
  const char *zName = PD("name","");
  /*
  ** Debugging Notes:
  **
  **    /announce/test1           ->   Shows query parameter values
  **    /announce/test2           ->   Shows the formatted message but does
  **                                   not send it.
  **    /announce/test3           ->   Sends the message, but also shows
  **                                   the SMTP transcript.
  */
  login_check_credentials();
  if( !g.perm.Announce ){
    login_needed(0);
    return;
  }
  if( !g.perm.Setup ){
    zName = 0;   /* Disable debugging feature for non-admin users */
  }
  style_set_current_feature("alerts");
  if( fossil_strcmp(zName,"test1")==0 ){
    /* Visit the /announce/test1 page to see the CGI variables */
    zAction = "announce/test1";
    @ <p style='border: 1px solid black; padding: 1ex;'>
    cgi_print_all(0, 0, 0);
    @ </p>
  }else if( P("submit")!=0 && cgi_csrf_safe(2) ){
    char *zErr = alert_send_announcement();
    style_header("Announcement Sent");
    if( zErr ){
      @ <h1>Error</h1>
      @ <p>The following error was reported by the
      @ announcement-sending subsystem:
      @ <blockquote><pre>
      @ %h(zErr)
      @ </pre></blockquote>
    }else{
      @ <p>The announcement has been sent.
      @ <a href="%h(PD("REQUEST_URI","/"))">Send another</a></p>
    }
    style_finish_page();
    return;
  } else if( !alert_enabled() ){
    style_header("Cannot Send Announcement");
    @ <p>Either you have no subscribers yet, or email alerts are not yet
    @ <a href="https://fossil-scm.org/fossil/doc/trunk/www/alerts.md">set up</a>
    @ for this repository.</p>
    return;
  }

  style_header("Send Announcement");
  alert_submenu_common();
  if( fossil_strcmp(zName,"test2")==0 ){
    zAction = "announce/test2";
  }else if( fossil_strcmp(zName,"test3")==0 ){
    zAction = "announce/test3";
  }
  @ <form method="POST" action="%R/%s(zAction)">
  login_insert_csrf_secret();
  @ <table class="subscribe">
  if( g.perm.Admin ){
    int aa = PB("aa");
    int all = PB("all");
    int aMod = PB("mods");
    const char *aack = aa ? "checked" : "";
    const char *allck = all ? "checked" : "";
    const char *modck = aMod ? "checked" : "";
    @ <tr>
    @  <td class="form_label">To:</td>
    @  <td><input type="text" name="to" value="%h(PT("to"))" size="30"><br>
    @  <label><input type="checkbox" name="aa" %s(aack)> \
    @  All "announcement" subscribers</label> \
    @  <a href="%R/subscribers?only=a" target="_blank">(list)</a><br>
    @  <label><input type="checkbox" name="all" %s(allck)> \
    @  All subscribers</label> \
    @  <a href="%R/subscribers" target="_blank">(list)</a><br>
    @  <label><input type="checkbox" name="mods" %s(modck)> \
    @  All moderators</label> \
    @  <a href="%R/setup_ulist?with=5" target="_blank">(list)</a><br></td>
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Subject:</td>
  @  <td><input type="text" name="subject" value="%h(PT("subject"))"\
  @  size="80"></td>
  @ </tr>
  @ <tr>
  @  <td class="form_label">Message:</td>
  @  <td><textarea name="msg" cols="80" rows="10" wrap="virtual">\
  @ %h(PT("msg"))</textarea>
  @ </tr>
  @ <tr>
  @   <td></td>
  if( fossil_strcmp(zName,"test2")==0 ){
    @   <td><input type="submit" name="submit" value="Dry Run">
  }else{
    @   <td><input type="submit" name="submit" value="Send Message">
  }
  @ </tr>
  @ </table>
  @ </form>
  if( g.perm.Setup ){
    @ <hr>
    @ <p>Trouble-shooting Options:</p>
    @ <ol>
    @ <li> <a href="%R/announce">Normal Processing</a>
    @ <li> Only <a href="%R/announce/test1">show POST parameters</a>
    @      - Do not send the announcement.
    @ <li> <a href="%R/announce/test2">Show the email text</a> but do
    @      not actually send it.
    @ <li> Send the message and also <a href="%R/announce/test3">show the
    @      SMTP traffic</a> when using "relay" mode.
    @ </ol>
  }
  style_finish_page();
}
