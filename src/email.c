/*
** Copyright (c) 2007 D. Richard Hipp
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
** Email notification features
*/
#include "config.h"
#include "email.h"
#include <assert.h>

/*
** Maximum size of the subscriberCode blob, in bytes
*/
#define SUBSCRIBER_CODE_SZ 32

/*
** SQL code to implement the tables needed by the email notification
** system.
*/
static const char zEmailInit[] =
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
@ --     t - Ticket changes
@ --     w - Wiki changes
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
@   smip TEXT                         -- IP address of last change
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
@   sentSep BOOLEAN DEFAULT false,    -- individual emails sent
@   sentDigest BOOLEAN DEFAULT false  -- digest emails sent
@ ) WITHOUT ROWID;
@ 
@ DROP TABLE IF EXISTS repository.email_bounce;
@ -- Record bounced emails.  If too many bounces are received within
@ -- some defined time range, then cancel the subscription.  Older
@ -- entries are periodically purged.
@ --
@ CREATE TABLE repository.email_bounce(
@   subscriberId INTEGER, -- to whom the email was sent.
@   sendTime INTEGER,     -- seconds since 1970 when email was sent
@   rcvdTime INTEGER      -- seconds since 1970 when bounce was received
@ );
;

/*
** Return true if the email notification tables exist.
*/
int email_tables_exist(void){
  return db_table_exists("repository", "subscriber");
}

/*
** Make sure the table needed for email notification exist in the repository.
**
** If the bOnlyIfEnabled option is true, then tables are only created
** if the email-send-method is something other than "off".
*/
void email_schema(int bOnlyIfEnabled){
  if( !email_tables_exist() ){
    if( bOnlyIfEnabled
     && fossil_strcmp(db_get("email-send-method","off"),"off")==0
    ){
      return;  /* Don't create table for disabled email */
    }
    db_multi_exec(zEmailInit/*works-like:""*/);
    email_triggers_enable();
  }
}

/*
** Enable triggers that automatically populate the pending_alert
** table.
*/
void email_triggers_enable(void){
  if( !db_table_exists("repository","pending_alert") ) return;
  db_multi_exec(
    "CREATE TRIGGER IF NOT EXISTS repository.email_trigger1\n"
    "AFTER INSERT ON event BEGIN\n"
    "  INSERT INTO pending_alert(eventid)\n"
    "    SELECT printf('%%.1c%%d',new.type,new.objid) WHERE true\n"
    "    ON CONFLICT(eventId) DO NOTHING;\n"
    "END;"
  );
}

/*
** Disable triggers the event_pending triggers.
**
** This must be called before rebuilding the EVENT table, for example
** via the "fossil rebuild" command.
*/
void email_triggers_disable(void){
  db_multi_exec(
    "DROP TRIGGER IF EXISTS repository.email_trigger1;\n"
  );
}

/*
** Return true if email alerts are active.
*/
int email_enabled(void){
  if( !email_tables_exist() ) return 0;
  if( fossil_strcmp(db_get("email-send-method","off"),"off")==0 ) return 0;
  return 1;
}

/*
** If the subscriber table does not exist, then paint an error message
** web page and return true.
**
** If the subscriber table does exist, return 0 without doing anything.
*/
static int email_webpages_disabled(void){
  if( email_tables_exist() ) return 0;
  style_header("Email Alerts Are Disabled");
  @ <p>Email alerts are disabled on this server</p>
  style_footer();
  return 1;
}

/*
** Insert a "Subscriber List" submenu link if the current user
** is an administrator.
*/
void email_submenu_common(void){
  if( g.perm.Admin ){
    if( fossil_strcmp(g.zPath,"subscribers") ){
      style_submenu_element("List Subscribers","%R/subscribers");
    }
    if( fossil_strcmp(g.zPath,"subscribe") ){
      style_submenu_element("Add New Subscriber","%R/subscribe");
    }
  }
}


/*
** WEBPAGE: setup_email
**
** Administrative page for configuring and controlling email notification.
** Normally accessible via the /Admin/Email menu.
*/
void setup_email(void){
  static const char *const azSendMethods[] = {
    "off",  "Disabled",
    "pipe", "Pipe to a command",
    "db",   "Store in a database",
    "dir",  "Store in a directory"
  };
  login_check_credentials();
  if( !g.perm.Setup ){
    login_needed(0);
    return;
  }
  db_begin_transaction();

  email_submenu_common();
  style_header("Email Notification Setup");
  @ <form action="%R/setup_email" method="post"><div>
  @ <input type="submit"  name="submit" value="Apply Changes" /><hr>
  login_insert_csrf_secret();

  entry_attribute("Canonical Server URL", 40, "email-url",
                   "eurl", "", 0);
  @ <p><b>Required.</b>
  @ This is URL used as the basename for hyperlinks included in
  @ email alert text.  Omit the trailing "/".
  @ Suggested value: "%h(g.zBaseURL)"
  @ (Property: "email-url")</p>
  @ <hr>

  entry_attribute("\"From\" email address", 20, "email-self",
                   "eself", "", 0);
  @ <p><b>Required.</b>
  @ This is the email from which email notifications are sent.  The
  @ system administrator should arrange for emails sent to this address
  @ to be handed off to the "fossil email incoming" command so that Fossil
  @ can handle bounces. (Property: "email-self")</p>
  @ <hr>

  entry_attribute("Repository Nickname", 16, "email-subname",
                   "enn", "", 0);
  @ <p><b>Required.</b>
  @ This is short name used to identifies the repository in the
  @ Subject: line of email alerts.  Traditionally this name is
  @ included in square brackets.  Examples: "[fossil-src]", "[sqlite-src]".
  @ (Property: "email-subname")</p>
  @ <hr>

  onoff_attribute("Automatic Email Exec", "email-autoexec",
                   "eauto", 0, 0);
  @ <p>If enabled, then email notifications are automatically
  @ dispatched after some webpages are accessed.  This eliminates the
  @ need to have a cron job running to invoke "fossil email exec"
  @ periodically.
  @ (Property: "email-autoexec")</p>
  @ <hr>

  multiple_choice_attribute("Email Send Method", "email-send-method", "esm",
       "off", count(azSendMethods)/2, azSendMethods);
  @ <p>How to send email.  The "Pipe to a command"
  @ method is the usual choice in production.
  @ (Property: "email-send-method")</p>
  @ <hr>
  email_schema(1);

  entry_attribute("Command To Pipe Email To", 80, "email-send-command",
                   "ecmd", "sendmail -t", 0);
  @ <p>When the send method is "pipe to a command", this is the command
  @ that is run.  Email messages are piped into the standard input of this
  @ command.  The command is expected to extract the sender address,
  @ recepient addresses, and subject from the header of the piped email
  @ text.  (Property: "email-send-command")</p>

  entry_attribute("Database In Which To Store Email", 60, "email-send-db",
                   "esdb", "", 0);
  @ <p>When the send method is "store in a databaes", each email message is
  @ stored in an SQLite database file with the name given here.
  @ (Property: "email-send-db")</p>

  entry_attribute("Directory In Which To Store Email", 60, "email-send-dir",
                   "esdir", "", 0);
  @ <p>When the send method is "store in a directory", each email message is
  @ stored as a separate file in the directory shown here.
  @ (Property: "email-send-dir")</p>
  @ <hr>

  entry_attribute("Administrator email address", 40, "email-admin",
                   "eadmin", "", 0);
  @ <p>This is the email for the human administrator for the system.
  @ Abuse and trouble reports are send here.
  @ (Property: "email-admin")</p>
  @ <hr>

  entry_attribute("Inbound email directory", 40, "email-receive-dir",
                   "erdir", "", 0);
  @ <p>Inbound emails can be stored in a directory for analysis as
  @ a debugging aid.  Put the name of that directory in this entry box.
  @ Disable saving of inbound email by making this an empty string.
  @ Abuse and trouble reports are send here.
  @ (Property: "email-receive-dir")</p>
  @ <hr>
  @ <p><input type="submit"  name="submit" value="Apply Changes" /></p>
  @ </div></form>
  db_end_transaction(0);
  style_footer();
}

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

/*
** Come up with a unique filename in the zDir directory.
**
** Space to hold the filename is obtained from mprintf() and must
** be freed using fossil_free() by the caller.
*/
static char *emailTempFilename(const char *zDir){
  char *zFile = db_text(0,
     "SELECT %Q||strftime('/%%Y%%m%%d%%H%%M%%S-','now')||hex(randomblob(8))",
        zDir);
  return zFile;
}

#if defined(_WIN32) || defined(WIN32)
# undef popen
# define popen _popen
# undef pclose
# define pclose _pclose
#endif

#if INTERFACE
/*
** An instance of the following object is used to send emails.
*/
struct EmailSender {
  sqlite3 *db;               /* Database emails are sent to */
  sqlite3_stmt *pStmt;       /* Stmt to insert into the database */
  const char *zDest;         /* How to send email. */
  const char *zDb;           /* Name of database file */
  const char *zDir;          /* Directory in which to store as email files */
  const char *zCmd;          /* Command to run for each email */
  const char *zFrom;         /* Emails come from here */
  Blob out;                  /* For zDest=="blob" */
  char *zErr;                /* Error message */
  int bImmediateFail;        /* On any error, call fossil_fatal() */
};
#endif /* INTERFACE */

/*
** Shutdown an emailer.  Clear all information other than the error message.
*/
static void emailerShutdown(EmailSender *p){
  sqlite3_finalize(p->pStmt);
  p->pStmt = 0;
  sqlite3_close(p->db);
  p->db = 0;
  p->zDb = 0;
  p->zDir = 0;
  p->zCmd = 0;
  p->zDest = "off";
  blob_zero(&p->out);
}

/*
** Put the EmailSender into an error state.
*/
static void emailerError(EmailSender *p, const char *zFormat, ...){
  va_list ap;
  fossil_free(p->zErr);
  va_start(ap, zFormat);
  p->zErr = vmprintf(zFormat, ap);
  va_end(ap);
  emailerShutdown(p);
  if( p->bImmediateFail ){
    fossil_fatal("%s", p->zErr);
  }
}

/*
** Free an email sender object
*/
void email_sender_free(EmailSender *p){
  emailerShutdown(p);
  fossil_free(p->zErr);
  fossil_free(p);
}

/*
** Get an email setting value.  Report an error if not configured.
** Return 0 on success and one if there is an error.
*/
static int emailerGetSetting(
  EmailSender *p,        /* Where to report the error */
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
** Create a new EmailSender object.
**
** The method used for sending email is determined by various email-*
** settings, and especially email-send-method.  The repository
** email-send-method can be overridden by the zAltDest argument to
** cause a different sending mechanism to be used.  Pass "stdout" to
** zAltDest to cause all emails to be printed to the console for
** debugging purposes.
**
** The EmailSender object returned must be freed using email_sender_free().
*/
EmailSender *email_sender_new(const char *zAltDest, int bImmediateFail){
  EmailSender *p;

  p = fossil_malloc(sizeof(*p));
  memset(p, 0, sizeof(*p));
  p->bImmediateFail = bImmediateFail;
  if( zAltDest ){
    p->zDest = zAltDest;
  }else{
    p->zDest = db_get("email-send-method","off");
  }
  if( fossil_strcmp(p->zDest,"off")==0 ) return p;
  if( emailerGetSetting(p, &p->zFrom, "email-self") ) return p;
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
  }
  return p;
}

/*
** Send a single email message.
**
** The recepient(s) must be specified using  "To:" or "Cc:" or "Bcc:" fields
** in the header.  Likewise, the header must contains a "Subject:" line.
** The header might also include fields like "Message-Id:" or
** "In-Reply-To:".
**
** This routine will add fields to the header as follows:
**
**     From:
**     Content-Type:
**     Content-Transfer-Encoding:
**     
** The caller maintains ownership of the input Blobs.  This routine will
** read the Blobs and send them onward to the email system, but it will
** not free them.
*/
void email_send(EmailSender *p, Blob *pHdr, Blob *pBody){
  Blob all, *pOut;
  if( fossil_strcmp(p->zDest, "off")==0 ){
    return;
  }
  if( fossil_strcmp(p->zDest, "blob")==0 ){
    pOut = &p->out;
    if( blob_size(pOut) ){
      blob_appendf(pOut, "%.72c\n", '=');
    }
  }else{
    blob_init(&all, 0, 0);
    pOut = &all;
  }
  blob_append(pOut, blob_buffer(pHdr), blob_size(pHdr));
  blob_appendf(pOut, "From: %s\r\n", p->zFrom);
  blob_add_final_newline(pBody);
  blob_appendf(pOut,"Content-Type: text/plain\r\n");
  blob_appendf(pOut, "Content-Transfer-Encoding: base64\r\n\r\n");
  append_base64(pOut, pBody);
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
      fclose(out);
    }else{
      emailerError(p, "Could not open output pipe \"%s\"", p->zCmd);
    }
  }else if( p->zDir ){
    char *zFile = emailTempFilename(p->zDir);
    blob_write_to_file(&all, zFile);
    fossil_free(zFile);
  }else if( strcmp(p->zDest, "stdout")==0 ){
    fossil_print("%s\n", blob_str(&all));
  }
  blob_zero(&all);
}

/*
** Analyze and act on a received email.
**
** This routine takes ownership of the Blob parameter and is responsible
** for freeing that blob when it is done with it.
**
** This routine acts on all email messages received from the
** "fossil email inbound" command.
*/
void email_receive(Blob *pMsg){
  /* To Do:  Look for bounce messages and possibly disable subscriptions */
  blob_zero(pMsg);
}

/*
** SETTING: email-send-method         width=5 default=off
** Determine the method used to send email.  Allowed values are
** "off", "pipe", "dir", "db", and "stdout".  The "off" value means
** no email is ever sent.  The "pipe" value means email messages are
** piped into a command determined by the email-send-command setting.
** The "dir" value means emails are written to individual files in a
** directory determined by the email-send-dir setting.  The "db" value
** means that emails are added to an SQLite database named by the
** email-send-db setting.  The "stdout" value writes email text to
** standard output, for debugging.
*/
/*
** SETTING: email-send-command       width=40
** This is a command to which outbound email content is piped when the
** email-send-method is set to "pipe".  The command must extract
** recipient, sender, subject, and all other relevant information
** from the email header.
*/
/*
** SETTING: email-send-dir           width=40
** This is a directory into which outbound emails are written as individual
** files if the email-send-method is set to "dir".
*/
/*
** SETTING: email-send-db            width=40
** This is an SQLite database file into which outbound emails are written
** if the email-send-method is set to "db".
*/
/*
** SETTING: email-self               width=40
** This is the email address for the repository.  Outbound emails add
** this email address as the "From:" field.
*/
/*
** SETTING: email-receive-dir         width=40
** Inbound email messages are saved as separate files in this directory,
** for debugging analysis.  Disable saving of inbound emails omitting
** this setting, or making it an empty string.
*/


/*
** COMMAND: email
** 
** Usage: %fossil email SUBCOMMAND ARGS...
**
** Subcommands:
**
**    exec                    Compose and send pending email alerts.
**                            Some installations may want to do this via
**                            a cron-job to make sure alerts are sent
**                            in a timely manner.
**                            Options:
**
**                               --digest     Send digests
**                               --test       Resets to standard output
**
**    inbound [FILE]          Receive an inbound email message.  This message
**                            is analyzed to see if it is a bounce, and if
**                            necessary, subscribers may be disabled.
**
**    reset                   Hard reset of all email notification tables
**                            in the repository.  This erases all subscription
**                            information.  Use with extreme care.
**
**    send TO [OPTIONS]       Send a single email message using whatever
**                            email sending mechanism is currently configured.
**                            Use this for testing the email configuration.
**                            Options:
**
**                              --body FILENAME
**                              --stdout
**                              --subject|-S SUBJECT
**
**    settings [NAME VALUE]   With no arguments, list all email settings.
**                            Or change the value of a single email setting.
**
**    subscribers [PATTERN]   List all subscribers matching PATTERN.
**
**    unsubscribe EMAIL       Remove a single subscriber with the given EMAIL.
*/
void email_cmd(void){
  const char *zCmd;
  int nCmd;
  db_find_and_open_repository(0, 0);
  email_schema(0);
  zCmd = g.argc>=3 ? g.argv[2] : "x";
  nCmd = (int)strlen(zCmd);
  if( strncmp(zCmd, "exec", nCmd)==0 ){
    u32 eFlags = 0;
    if( find_option("digest",0,0)!=0 ) eFlags |= SENDALERT_DIGEST;
    if( find_option("test",0,0)!=0 ){
      eFlags |= SENDALERT_PRESERVE|SENDALERT_STDOUT;
    }
    verify_all_options();
    email_send_alerts(eFlags);
  }else
  if( strncmp(zCmd, "inbound", nCmd)==0 ){
    Blob email;
    const char *zInboundDir = db_get("email-receive-dir","");
    verify_all_options();
    if( g.argc!=3 && g.argc!=4 ){
      usage("inbound [FILE]");
    }
    blob_read_from_file(&email, g.argc==3 ? "-" : g.argv[3], ExtFILE);
    if( zInboundDir[0] ){
      char *zFN = emailTempFilename(zInboundDir);
      blob_write_to_file(&email, zFN);
      fossil_free(zFN);
    }
    email_receive(&email);
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
      blob_zero(&yn);
    }
    if( c=='y' ){
      email_triggers_disable();
      db_multi_exec(
        "DROP TABLE IF EXISTS subscriber;\n"
        "DROP TABLE IF EXISTS pending_alert;\n"
        "DROP TABLE IF EXISTS email_bounce;\n"
        /* Legacy */
        "DROP TABLE IF EXISTS email_pending;\n"
        "DROP TABLE IF EXISTS subscription;\n"
      );
      email_schema(0);
    }
  }else
  if( strncmp(zCmd, "send", nCmd)==0 ){
    Blob prompt, body, hdr;
    const char *zDest = find_option("stdout",0,0)!=0 ? "stdout" : 0;
    int i;
    const char *zSubject = find_option("subject", "S", 1);
    const char *zSource = find_option("body", 0, 1);
    EmailSender *pSender;
    verify_all_options();
    blob_init(&prompt, 0, 0);
    blob_init(&body, 0, 0);
    blob_init(&hdr, 0, 0);
    for(i=3; i<g.argc; i++){
      blob_appendf(&hdr, "To: %s\n", g.argv[i]);
    }
    if( zSubject ){
      blob_appendf(&hdr, "Subject: %s\n", zSubject);
    }
    if( zSource ){
      blob_read_from_file(&body, zSource, ExtFILE);
    }else{
      prompt_for_user_comment(&body, &prompt);
    }
    blob_add_final_newline(&body);
    pSender = email_sender_new(zDest, 1);
    email_send(pSender, &hdr, &body);
    email_sender_free(pSender);
    blob_zero(&hdr);
    blob_zero(&body);
    blob_zero(&prompt);
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
      db_set(pSetting->name, g.argv[4], isGlobal);
      g.argc = 3;
    }
    pSetting = setting_info(&nSetting);
    for(; nSetting>0; nSetting--, pSetting++ ){
      if( strncmp(pSetting->name,"email-",6)!=0 ) continue;
      print_setting(pSetting);
    }
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
  if( strncmp(zCmd, "unsubscribe", nCmd)==0 ){
    verify_all_options();
    if( g.argc!=4 ) usage("unsubscribe EMAIL");
    db_multi_exec(
      "DELETE FROM subscriber WHERE semail=%Q", g.argv[3]);
  }else
  {
    usage("exec|inbound|reset|send|setting|subscribers|unsubscribe");
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

  /* Check the validity of the email address.
  **
  **  (1) Exactly one '@' character.
  **  (2) No other characters besides [a-zA-Z0-9._-]
  */
  zEAddr = P("e");
  if( zEAddr==0 ) return 0;
  for(i=j=0; (c = zEAddr[i])!=0; i++){
    if( c=='@' ){
      n = i;
      j++;
      continue;
    }
    if( !fossil_isalnum(c) && c!='.' && c!='_' && c!='-' ){
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

  /* Verify the captcha */
  if( needCaptcha && !captcha_is_correct(1) ){
    *peErr = 2;
    *pzErr = mprintf("incorrect security code");
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
** The Email-Alerts permission ("7") is required to access this
** page.  To allow anonymous passers-by to sign up for email
** notification, set Email-Alerts on user "nobody" or "anonymous".
*/
void subscribe_page(void){
  int needCaptcha;
  unsigned int uSeed;
  const char *zDecoded;
  char *zCaptcha = 0;
  char *zErr = 0;
  int eErr = 0;

  if( email_webpages_disabled() ) return;
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
  email_submenu_common();
  needCaptcha = !login_is_individual();
  if( P("submit")
   && cgi_csrf_safe(1)
   && subscribe_error_check(&eErr,&zErr,needCaptcha)
  ){
    /* A validated request for a new subscription has been received. */
    char ssub[20];
    const char *zEAddr = P("e");
    sqlite3_int64 id;   /* New subscriber Id */
    const char *zCode;  /* New subscriber code (in hex) */
    int nsub = 0;
    const char *suname = PT("suname");
    if( suname==0 && needCaptcha==0 && !g.perm.Admin ) suname = g.zLogin;
    if( suname && suname[0]==0 ) suname = 0;
    if( PB("sa") ) ssub[nsub++] = 'a';
    if( PB("sc") ) ssub[nsub++] = 'c';
    if( PB("st") ) ssub[nsub++] = 't';
    if( PB("sw") ) ssub[nsub++] = 'w';
    ssub[nsub] = 0;
    db_multi_exec(
      "INSERT INTO subscriber(semail,suname,"
      "  sverified,sdonotcall,sdigest,ssub,sctime,mtime,smip)"
      "VALUES(%Q,%Q,%d,0,%d,%Q,now(),now(),%Q)",
      /* semail */    zEAddr,
      /* suname */    suname,
      /* sverified */ needCaptcha==0,
      /* sdigest */   PB("di"),
      /* ssub */      ssub,
      /* smip */      g.zIpAddr
    );
    id = db_last_insert_rowid();
    zCode = db_text(0,
         "SELECT hex(subscriberCode) FROM subscriber WHERE subscriberId=%lld",
         id);
    if( !needCaptcha ){
      /* The new subscription has been added on behalf of a logged-in user.
      ** No verification is required.  Jump immediately to /alerts page.
      */
      cgi_redirectf("%R/alerts/%s", zCode);
      return;
    }else{
      /* We need to send a verification email */
      Blob hdr, body;
      EmailSender *pSender = email_sender_new(0,0);
      blob_init(&hdr,0,0);
      blob_init(&body,0,0);
      blob_appendf(&hdr, "To: %s\n", zEAddr);
      blob_appendf(&hdr, "Subject: Subscription verification\n");
      blob_appendf(&body, zConfirmMsg/*works-like:"%s%s%s"*/,
                   g.zBaseURL, g.zBaseURL, zCode);
      email_send(pSender, &hdr, &body);
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
        @ hyperlink that you must click on in order to activate your
        @ subscription.</p>
      }
      email_sender_free(pSender);
      style_footer();
    }
    return;
  }
  style_header("Signup For Email Alerts");
  @ <p>To receive email notifications for changes to this
  @ repository, fill out the form below and press "Submit" button.</p>
  form_begin(0, "%R/subscribe");
  @ <table class="subscribe">
  @ <tr>
  @  <td class="form_label">Email&nbsp;Address:</td>
  @  <td><input type="text" name="e" value="%h(PD("e",""))" size="30"></td>
  if( eErr==1 ){
    @  <td><span class="loginError">&larr; %h(zErr)</span></td>
  }
  @ </tr>
  if( needCaptcha ){
    uSeed = captcha_seed();
    zDecoded = captcha_decode(uSeed);
    zCaptcha = captcha_render(zDecoded);
    @ <tr>
    @  <td class="form_label">Security Code:</td>
    @  <td><input type="text" name="captcha" value="" size="30">
    @  <input type="hidden" name="captchaseed" value="%u(uSeed)"></td>
    if( eErr==2 ){
      @  <td><span class="loginError">&larr; %h(zErr)</span></td>
    }
    @ </tr>
  }
  if( g.perm.Admin ){
    @ <tr>
    @  <td class="form_label">User:</td>
    @  <td><input type="text" name="suname" value="%h(PD("suname",g.zLogin))" \
    @  size="30"></td>
    if( eErr==3 ){
      @  <td><span class="loginError">&larr; %h(zErr)</span></td>
    }
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Options:</td>
  @  <td><label><input type="checkbox" name="sa" %s(PCK("sa"))> \
  @  Announcements</label><br>
  @  <label><input type="checkbox" name="sc" %s(PCK("sc"))> \
  @  Check-ins</label><br>
  @  <label><input type="checkbox" name="st" %s(PCK("st"))> \
  @  Ticket changes</label><br>
  @  <label><input type="checkbox" name="sw" %s(PCK("sw"))> \
  @  Wiki</label><br>
  @  <label><input type="checkbox" name="di" %s(PCK("di"))> \
  @  Daily digest only</label><br>
  if( g.perm.Admin ){
    @  <label><input type="checkbox" name="vi" %s(PCK("vi"))> \
    @  Verified</label><br>
    @  <label><input type="checkbox" name="dnc" %s(PCK("dnc"))> \
    @  Do not call</label><br>
  }
  @ </td>
  @ </tr>
  @ <tr>
  @  <td></td>
  if( needCaptcha && !email_enabled() ){
    @  <td><input type="submit" name="submit" value="Submit" disabled>
    @  (Email current disabled)</td>
  }else{
    @  <td><input type="submit" name="submit" value="Submit"></td>
  }
  @ </tr>
  @ </table>
  if( needCaptcha ){
    @ <div class="captcha"><table class="captcha"><tr><td><pre>
    @ %h(zCaptcha)
    @ </pre>
    @ Enter the 8 characters above in the "Security Code" box
    @ </td></tr></table></div>
  }
  @ </form>
  fossil_free(zErr);
  style_footer();
}

/*
** Either shutdown or completely delete a subscription entry given
** by the hex value zName.  Then paint a webpage that explains that
** the entry has been removed.
*/
static void email_unsubscribe(const char *zName){
  char *zEmail;
  zEmail = db_text(0, "SELECT semail FROM subscriber"
                      " WHERE subscriberCode=hextoblob(%Q)", zName);
  if( zEmail==0 ){
    style_header("Unsubscribe Fail");
    @ <p>Unable to locate a subscriber with the requested key</p>
  }else{
    db_multi_exec(
      "DELETE FROM subscriber WHERE subscriberCode=hextoblob(%Q)",
      zName
    );
    style_header("Unsubscribed");
    @ <p>The "%h(zEmail)" email address has been delisted.
    @ All traces of that email address have been removed</p>
  }
  style_footer();
  return;
}

/*
** WEBPAGE: alerts
**
** Edit email alert and notification settings.
**
** The subscriber is identified in either of two ways:
**
**    (1)  The name= query parameter contains the subscriberCode.
**         
**    (2)  The user is logged into an account other than "nobody" or
**         "anonymous".  In that case the notification settings
**         associated with that account can be edited without needing
**         to know the subscriber code.
*/
void alerts_page(void){
  const char *zName = P("name");
  Stmt q;
  int sa, sc, st, sw;
  int sdigest, sdonotcall, sverified;
  const char *ssub;
  const char *semail;
  const char *smip;
  const char *suname;
  const char *mtime;
  const char *sctime;
  int eErr = 0;
  char *zErr = 0;

  if( email_webpages_disabled() ) return;
  login_check_credentials();
  if( zName==0 && login_is_individual() ){
    zName = db_text(0, "SELECT hex(subscriberCode) FROM subscriber"
                       " WHERE suname=%Q", g.zLogin);
  }
  if( zName==0 || !validate16(zName, -1) ){
    cgi_redirect("subscribe");
    return;
  }
  email_submenu_common();
  if( P("submit")!=0 && cgi_csrf_safe(1) ){
    int sdonotcall = PB("sdonotcall");
    int sdigest = PB("sdigest");
    char ssub[10];
    int nsub = 0;
    if( PB("sa") ) ssub[nsub++] = 'a';
    if( PB("sc") ) ssub[nsub++] = 'c';
    if( PB("st") ) ssub[nsub++] = 't';
    if( PB("sw") ) ssub[nsub++] = 'w';
    ssub[nsub] = 0;
    if( g.perm.Admin ){
      const char *suname = PT("suname");
      if( suname && suname[0]==0 ) suname = 0;
      int sverified = PB("sverified");
      db_multi_exec(
        "UPDATE subscriber SET"
        " sdonotcall=%d,"
        " sdigest=%d,"
        " ssub=%Q,"
        " mtime=strftime('%%s','now'),"
        " smip=%Q,"
        " suname=%Q,"
        " sverified=%d"
        " WHERE subscriberCode=hextoblob(%Q)",
        sdonotcall,
        sdigest,
        ssub,
        g.zIpAddr,
        suname,
        sverified,
        zName
      );
    }else{
      db_multi_exec(
        "UPDATE subscriber SET"
        " sdonotcall=%d,"
        " sdigest=%d,"
        " ssub=%Q,"
        " mtime=strftime('%%s','now'),"
        " smip=%Q"
        " WHERE subscriberCode=hextoblob(%Q)",
        sdonotcall,
        sdigest,
        ssub,
        g.zIpAddr,
        zName
      );
    }
  }
  if( P("delete")!=0 && cgi_csrf_safe(1) ){
    if( !PB("dodelete") ){
      eErr = 9;
      zErr = mprintf("Select this checkbox and press \"Unsubscribe\" to"
                     " unsubscribe");
    }else{
      email_unsubscribe(zName);
      return;
    }
  }
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
    "  datetime(sctime,'unixepoch')"  /* 8 */
    " FROM subscriber WHERE subscriberCode=hextoblob(%Q)", zName);
  if( db_step(&q)!=SQLITE_ROW ){
    db_finalize(&q);
    cgi_redirect("subscribe");
    return;
  }
  style_header("Update Subscription");
  semail = db_column_text(&q, 0);
  sverified = db_column_int(&q, 1);
  sdonotcall = db_column_int(&q, 2);
  sdigest = db_column_int(&q, 3);
  ssub = db_column_text(&q, 4);
  sa = strchr(ssub,'a')!=0;
  sc = strchr(ssub,'c')!=0;
  st = strchr(ssub,'t')!=0;
  sw = strchr(ssub,'w')!=0;
  smip = db_column_text(&q, 5);
  suname = db_column_text(&q, 6);
  mtime = db_column_text(&q, 7);
  sctime = db_column_text(&q, 8);
  if( !g.perm.Admin && !sverified ){
    db_multi_exec(
      "UPDATE subscriber SET sverified=1 WHERE subscriberCode=hextoblob(%Q)",
      zName);
    @ <h1>Your email alert subscription has been verified!</h1>
    @ <p>Use the form below to update your subscription information.</p>
    @ <p>Hint:  Bookmark this page so that you can more easily update
    @ your subscription information in the future</p>
  }else{
    @ <p>Make changes to the email subscription shown below and
    @ press "Submit".</p>
  }
  form_begin(0, "%R/alerts");
  @ <input type="hidden" name="name" value="%h(zName)">
  @ <table class="subscribe">
  @ <tr>
  @  <td class="form_label">Email&nbsp;Address:</td>
  @  <td>%h(semail)</td>
  @ </tr>
  if( g.perm.Admin ){
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
    @  <td class="form_label">User:</td>
    @  <td><input type="text" name="suname" value="%h(suname?suname:"")" \
    @  size="30"></td>
    @ </tr>
  }
  @ <tr>
  @  <td class="form_label">Options:</td>
  @  <td><label><input type="checkbox" name="sa" %s(sa?"checked":"")>\
  @  Announcements</label><br>
  @  <label><input type="checkbox" name="sc" %s(sc?"checked":"")>\
  @  Check-ins</label><br>
  @  <label><input type="checkbox" name="st" %s(st?"checked":"")>\
  @  Ticket changes</label><br>
  @  <label><input type="checkbox" name="sw" %s(sw?"checked":"")>\
  @  Wiki</label><br>
  @  <label><input type="checkbox" name="sdigest" %s(sdigest?"checked":"")>\
  @  Daily digest only</label><br>
  if( g.perm.Admin ){
    @  <label><input type="checkbox" name="sdonotcall" \
    @  %s(sdonotcall?"checked":"")> Do not call</label><br>
    @  <label><input type="checkbox" name="sverified" \
    @  %s(sverified?"checked":"")>\
    @  Verified</label><br>
  }
  @  <label><input type="checkbox" name="dodelete">
  @  Unsubscribe</label> \
  if( eErr==9 ){
    @ <span class="loginError">&larr; %h(zErr)</span>\
  }
  @ <br>
  @ </td></tr>
  @ <tr>
  @  <td></td>
  @  <td><input type="submit" name="submit" value="Submit">
  @  <input type="submit" name="delete" value="Unsubscribe">
  @ </tr>
  @ </table>
  @ </form>
  fossil_free(zErr);
  db_finalize(&q);
  style_footer();
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
*/
void unsubscribe_page(void){
  const char *zName = P("name");
  char *zErr = 0;
  int eErr = 0;
  unsigned int uSeed;
  const char *zDecoded;
  char *zCaptcha = 0;
  int dx;
  int bSubmit;
  const char *zEAddr;
  char *zCode = 0;

  /* If a valid subscriber code is supplied, then unsubscribe immediately.
  */
  if( zName 
   && db_exists("SELECT 1 FROM subscriber WHERE subscriberCode=hextoblob(%Q)",
                zName)
  ){
    email_unsubscribe(zName);
    return;
  }

  /* Logged in users are redirected to the /alerts page */
  login_check_credentials();
  if( login_is_individual() ){
    cgi_redirectf("%R/alerts");
    return;
  }

  zEAddr = PD("e","");
  dx = atoi(PD("dx","0"));
  bSubmit = P("submit")!=0 && P("e")!=0 && cgi_csrf_safe(1);
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
    EmailSender *pSender = email_sender_new(0,0);
    blob_init(&hdr,0,0);
    blob_init(&body,0,0);
    blob_appendf(&hdr, "To: %s\n", zEAddr);
    blob_appendf(&hdr, "Subject: Unsubscribe Instructions\n");
    blob_appendf(&body, zUnsubMsg/*works-like:"%s%s%s%s%s%s"*/,
                  g.zBaseURL, g.zBaseURL, zCode, g.zBaseURL, g.zBaseURL, zCode);
    email_send(pSender, &hdr, &body);
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
    email_sender_free(pSender);
    style_footer();
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
  zDecoded = captcha_decode(uSeed);
  zCaptcha = captcha_render(zDecoded);
  @ <tr>
  @  <td class="form_label">Security Code:</td>
  @  <td><input type="text" name="captcha" value="" size="30">
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
  @ <div class="captcha"><table class="captcha"><tr><td><pre>
  @ %h(zCaptcha)
  @ </pre>
  @ Enter the 8 characters above in the "Security Code" box
  @ </td></tr></table></div>
  @ </form>
  fossil_free(zErr);
  style_footer();
}

/*
** WEBPAGE: subscribers
**
** This page, accessible to administrators only,
** shows a list of email notification email addresses.
** Clicking on an email takes one to the /alerts page
** for that email where the delivery settings can be
** modified.
*/
void subscriber_list_page(void){
  Blob sql;
  Stmt q;
  double rNow;
  if( email_webpages_disabled() ) return;
  login_check_credentials();
  if( !g.perm.Admin ){
    login_needed(0);
    return;
  }
  email_submenu_common();
  style_header("Subscriber List");
  blob_init(&sql, 0, 0);
  blob_append_sql(&sql,
    "SELECT hex(subscriberCode),"          /* 0 */
    "       semail,"                       /* 1 */
    "       ssub,"                         /* 2 */
    "       suname,"                       /* 3 */
    "       sverified,"                    /* 4 */
    "       sdigest,"                      /* 5 */
    "       date(sctime,'unixepoch'),"     /* 6 */
    "       julianday(mtime,'unixepoch')"  /* 7 */
    " FROM subscriber"
  );
  db_prepare_blob(&q, &sql);
  rNow = db_double(0.0,"SELECT julianday('now')");
  @ <table border="1">
  @ <tr>
  @ <th>Email
  @ <th>Events
  @ <th>Digest-Only?
  @ <th>User
  @ <th>Verified?
  @ <th>Last change
  @ <th>Created
  @ </tr>
  while( db_step(&q)==SQLITE_ROW ){
    double rAge = rNow - db_column_double(&q, 7);
    @ <tr>
    @ <td><a href='%R/alerts/%s(db_column_text(&q,0))'>\
    @ %h(db_column_text(&q,1))</a></td>
    @ <td>%h(db_column_text(&q,2))</td>
    @ <td>%s(db_column_int(&q,5)?"digest":"")</td>
    @ <td>%h(db_column_text(&q,3))</td>
    @ <td>%s(db_column_int(&q,4)?"yes":"pending")</td>
    @ <td>%z(human_readable_age(rAge))</td>
    @ <td>%h(db_column_text(&q,6))</td>
    @ </tr>
  }
  @ </table>
  db_finalize(&q);
  style_footer();
}

#if LOCAL_INTERFACE
/*
** A single event that might appear in an alert is recorded as an
** instance of the following object.
*/
struct EmailEvent {
  int type;          /* 'c', 't', 'w', etc. */
  Blob txt;          /* Text description to appear in an alert */
  EmailEvent *pNext; /* Next in chronological order */
};
#endif

/*
** Free a linked list of EmailEvent objects
*/
void email_free_eventlist(EmailEvent *p){
  while( p ){
    EmailEvent *pNext = p->pNext;
    blob_zero(&p->txt);
    fossil_free(p);
    p = pNext;
  }
}

/*
** Compute and return a linked list of EmailEvent objects
** corresponding to the current content of the temp.wantalert
** table which should be defined as follows:
**
**     CREATE TEMP TABLE wantalert(eventId TEXT);
*/
EmailEvent *email_compute_event_text(int *pnEvent){
  Stmt q;
  EmailEvent *p;
  EmailEvent anchor;
  EmailEvent *pLast;
  const char *zUrl = db_get("email-url","http://localhost:8080");

  db_prepare(&q,
    "SELECT"
    " blob.uuid,"  /* 0 */
    " datetime(event.mtime),"  /* 1 */
    " coalesce(ecomment,comment)"
    "  || ' (user: ' || coalesce(euser,user,'?')"
    "  || (SELECT case when length(x)>0 then ' tags: ' || x else '' end"
    "      FROM (SELECT group_concat(substr(tagname,5), ', ') AS x"
    "              FROM tag, tagxref"
    "             WHERE tagname GLOB 'sym-*' AND tag.tagid=tagxref.tagid"
    "               AND tagxref.rid=blob.rid AND tagxref.tagtype>0))"
    "  || ')' as comment,"  /* 2 */
    " tagxref.value AS branch,"  /* 3 */
    " wantalert.eventId"     /* 4 */
    " FROM temp.wantalert JOIN tag CROSS JOIN event CROSS JOIN blob"
    "  LEFT JOIN tagxref ON tagxref.tagid=tag.tagid"
    "                       AND tagxref.tagtype>0"
    "                       AND tagxref.rid=blob.rid"
    " WHERE blob.rid=event.objid"
    "   AND tag.tagname='branch'"
    "   AND event.objid=substr(wantalert.eventId,2)+0"
    " ORDER BY event.mtime"
  );
  memset(&anchor, 0, sizeof(anchor));
  pLast = &anchor;
  *pnEvent = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zType = "";
    p = fossil_malloc( sizeof(EmailEvent) );
    pLast->pNext = p;
    pLast = p;
    p->type = db_column_text(&q, 4)[0];
    p->pNext = 0;
    switch( p->type ){
      case 'c':  zType = "Check-In";        break;
      case 't':  zType = "Wiki Edit";       break;
      case 'w':  zType = "Ticket Change";   break;
    }
    blob_init(&p->txt, 0, 0);
    blob_appendf(&p->txt,"== %s %s ==\n%s\n%s/info/%.20s\n",
      db_column_text(&q,1),
      zType,
      db_column_text(&q,2),
      zUrl,
      db_column_text(&q,0)
    );
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
** Append the "unsubscribe" notification and other footer text to
** the end of an email alert being assemblied in pOut.
*/
void email_footer(Blob *pOut){
  blob_appendf(pOut, "\n%.72c\nTo unsubscribe: %s/unsubscribe\n",
     '-', db_get("email-url","http://localhost:8080"));
}

/*
** COMMAND:  test-alert
**
** Usage: %fossil test-alert EVENTID ...
**
** Generate the text of an email alert for all of the EVENTIDs
** listed on the command-line.  Or if no events are listed on the
** command line, generate text for all events named in the
** pending_alert table.
**
** This command is intended for testing and debugging the logic
** that generates email alert text.
*/
void test_alert_cmd(void){
  Blob out;
  int nEvent;
  EmailEvent *pEvent, *p;

  db_find_and_open_repository(0, 0);
  verify_all_options();
  db_begin_transaction();
  email_schema(0);
  db_multi_exec("CREATE TEMP TABLE wantalert(eventid TEXT)");
  if( g.argc==2 ){
    db_multi_exec("INSERT INTO wantalert SELECT eventid FROM pending_alert");
  }else{
    int i;
    for(i=2; i<g.argc; i++){
      db_multi_exec("INSERT INTO wantalert VALUES(%Q)", g.argv[i]);
    }
  }
  blob_init(&out, 0, 0);
  email_header(&out);
  pEvent = email_compute_event_text(&nEvent);
  for(p=pEvent; p; p=p->pNext){
    blob_append(&out, "\n", 1);
    blob_append(&out, blob_buffer(&p->txt), blob_size(&p->txt));
  }
  email_free_eventlist(pEvent);
  email_footer(&out);
  fossil_print("%s", blob_str(&out));
  blob_zero(&out);
  db_end_transaction(0);
}

/*
** COMMAND:  test-add-alerts
**
** Usage: %fossil test-add-alerts EVENTID ...
**
** Add one or more events to the pending_alert queue.  Use this
** command during testing to force email notifications for specific
** events.
**
** EVENTIDs are text.  The first character is 'c', 'w', or 't'
** for check-in, wiki, or ticket.  The remaining text is a
** integer that references the EVENT.OBJID value for the event.
** Run /timeline?showid to see these OBJID values.
*/
void test_add_alert_cmd(void){
  int i;
  db_find_and_open_repository(0, 0);
  verify_all_options();
  db_begin_transaction();
  email_schema(0);
  for(i=2; i<g.argc; i++){
    db_multi_exec("REPLACE INTO pending_alert(eventId) VALUES(%Q)", g.argv[i]);
  }
  db_end_transaction(0);
}

#if INTERFACE
/*
** Flags for email_send_alerts()
*/
#define SENDALERT_DIGEST      0x0001    /* Send a digest */
#define SENDALERT_PRESERVE    0x0002    /* Do not mark the task as done */
#define SENDALERT_STDOUT      0x0004    /* Print emails instead of sending */

#endif /* INTERFACE */

/*
** Send alert emails to all subscribers.
*/
void email_send_alerts(u32 flags){
  EmailEvent *pEvents, *p;
  int nEvent = 0;
  Stmt q;
  const char *zDigest = "false";
  Blob hdr, body;
  const char *zUrl;
  const char *zRepoName;
  const char *zFrom;
  const char *zDest = (flags & SENDALERT_STDOUT) ? "stdout" : 0;
  EmailSender *pSender = 0;

  if( g.fSqlTrace ) fossil_trace("-- BEGIN email_send_alerts(%u)\n", flags);
  db_begin_transaction();
  if( !email_enabled() ) goto send_alerts_done;
  zUrl = db_get("email-url",0);
  if( zUrl==0 ) goto send_alerts_done;
  zRepoName = db_get("email-subname",0);
  if( zRepoName==0 ) goto send_alerts_done;
  zFrom = db_get("email-self",0);
  if( zFrom==0 ) goto send_alerts_done;
  pSender = email_sender_new(zDest, 0);
  db_multi_exec(
    "DROP TABLE IF EXISTS temp.wantalert;"
    "CREATE TEMP TABLE wantalert(eventId TEXT);"
  );
  if( flags & SENDALERT_DIGEST ){
    db_multi_exec(
      "INSERT INTO wantalert SELECT eventid FROM pending_alert"
      "  WHERE sentDigest IS FALSE"
    );
    zDigest = "true";
  }else{
    db_multi_exec(
      "INSERT INTO wantalert SELECT eventid FROM pending_alert"
      "  WHERE sentSep IS FALSE"
    );
  }
  pEvents = email_compute_event_text(&nEvent);
  if( nEvent==0 ) goto send_alerts_done;
  blob_init(&hdr, 0, 0);
  blob_init(&body, 0, 0);
  db_prepare(&q,
     "SELECT"
     " hex(subscriberCode),"  /* 0 */
     " semail,"               /* 1 */
     " ssub"                  /* 2 */
     " FROM subscriber"
     " WHERE sverified AND NOT sdonotcall"
     "  AND sdigest IS %s",
     zDigest/*safe-for-%s*/
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zCode = db_column_text(&q, 0);
    const char *zSub = db_column_text(&q, 2);
    const char *zEmail = db_column_text(&q, 1);
    int nHit = 0;
    for(p=pEvents; p; p=p->pNext){
      if( strchr(zSub,p->type)==0 ) continue;
      if( nHit==0 ){
        blob_appendf(&hdr,"To: %s\n", zEmail);
        blob_appendf(&hdr,"Subject: %s activity alert\n", zRepoName);
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
    if( nHit==0 ) continue;
    blob_appendf(&body,"\n%.72c\nSubscription info: %s/alerts/%s\n",
         '-', zUrl, zCode);
    email_send(pSender,&hdr,&body);
    blob_truncate(&hdr, 0);
    blob_truncate(&body, 0);
  }
  blob_zero(&hdr);
  blob_zero(&body);
  db_finalize(&q);
  email_free_eventlist(pEvents);
  if( (flags & SENDALERT_PRESERVE)==0 ){
    if( flags & SENDALERT_DIGEST ){
      db_multi_exec("UPDATE pending_alert SET sentDigest=true");
    }else{
      db_multi_exec("UPDATE pending_alert SET sentSep=true");
    }
    db_multi_exec("DELETE FROM pending_alert WHERE sentDigest AND sentSep");
  }
send_alerts_done:
  email_sender_free(pSender);
  if( g.fSqlTrace ) fossil_trace("-- END email_send_alerts(%u)\n", flags);
  db_end_transaction(0);
}

/*
** Check to see if any email notifications need to occur, and then
** do them.
**
** This routine is called after certain webpages have been run and
** have already responded.
*/
void email_auto_exec(void){
  int iJulianDay;
  if( g.db==0 ) return;
  if( db_transaction_nesting_depth()!=0 ){
    fossil_warning("Called email_auto_exec() from within a transaction");
    return;
  }
  db_begin_transaction();
  if( !email_tables_exist() ) goto autoexec_done;
  if( !db_get_boolean("email-autoexec",0) ) goto autoexec_done;
  email_send_alerts(0);
  iJulianDay = db_int(0, "SELECT julianday('now')");
  if( iJulianDay>db_get_int("email-last-digest",0) ){
    if( db_transaction_nesting_depth()!=1 ){
      fossil_warning("Transaction nesting error prior to digest processing");
    }else{
      db_set_int("email-last-digest",iJulianDay,0);
      email_send_alerts(SENDALERT_DIGEST);
    }
  }

autoexec_done:
  db_end_transaction(0);
}

/*
** WEBPAGE: msgtoadmin
**
** A web-form to send a message to the repository administrator.
*/
void msgtoadmin_page(void){
  const char *zAdminEmail = db_get("email-admin",0);
  unsigned int uSeed;
  const char *zDecoded;
  char *zCaptcha = 0;

  login_check_credentials();
  if( zAdminEmail==0 || zAdminEmail[0]==0 ){
    style_header("Admin Messaging Disabled");
    @ <p>Messages to the administrator are disabled on this repository
    style_footer();
    return;
  }
  if( P("submit")!=0 
   && P("subject")!=0
   && P("msg")!=0
   && P("from")!=0
   && cgi_csrf_safe(1)
   && captcha_is_correct(0)
  ){
    Blob hdr, body;
    EmailSender *pSender = email_sender_new(0,0);
    blob_init(&hdr, 0, 0);
    blob_appendf(&hdr, "To: %s\nSubject: %s administrator message\n",
                 zAdminEmail, db_get("email-subname","Fossil Repo"));
    blob_init(&body, 0, 0);
    blob_appendf(&body, "Message from [%s]\n", PT("from")/*safe-for-%s*/);
    blob_appendf(&body, "Subject: [%s]\n\n", PT("subject")/*safe-for-%s*/);
    blob_appendf(&body, "%s", PT("msg")/*safe-for-%s*/);
    email_send(pSender, &hdr, &body);
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
    email_sender_free(pSender);
    style_footer();
    return;
  }
  if( captcha_needed() ){
    uSeed = captcha_seed();
    zDecoded = captcha_decode(uSeed);
    zCaptcha = captcha_render(zDecoded);
  }
  style_header("Message To Administrator");
  form_begin(0, "%R/msgtoadmin");
  @ <p>Enter a message to the repository administrator below:</p>
  @ <table class="subscribe">
  if( zCaptcha ){
    @ <tr>
    @  <td class="form_label">Security&nbsp;Code:</td>
    @  <td><input type="text" name="captcha" value="" size="10">
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
    @ <div class="captcha"><table class="captcha"><tr><td><pre>
    @ %h(zCaptcha)
    @ </pre>
    @ Enter the 8 characters above in the "Security Code" box
    @ </td></tr></table></div>
  }
  @ </form>
  style_footer();
}

/*
** Send an annoucement message described by query parameter.
** Permission to do this has already been verified.
*/
static char *email_send_announcement(void){
  EmailSender *pSender;
  char *zErr;
  const char *zTo = PT("to");
  char *zSubject = PT("subject");
  int bAll = PB("all");
  int bAA = PB("aa");
  const char *zSub = db_get("email-subname", "[Fossil Repo]");
  int bTest2 = fossil_strcmp(P("name"),"test2")==0;
  Blob hdr, body;
  blob_init(&body, 0, 0);
  blob_init(&hdr, 0, 0);
  blob_appendf(&body, "%s", PT("msg")/*safe-for-%s*/);
  pSender = email_sender_new(bTest2 ? "blob" : 0, 0);
  if( zTo[0] ){
    blob_appendf(&hdr, "To: %s\nSubject: %s %s\n", zTo, zSub, zSubject);
    email_send(pSender, &hdr, &body);
  }
  if( bAll || bAA ){
    Stmt q;
    int nUsed = blob_size(&body);
    const char *zURL =  db_get("email-url",0);
    db_prepare(&q, "SELECT semail, subscriberCode FROM subscriber "
                   " WHERE sverified AND NOT sdonotcall %s",
                   bAll ? "" : " AND ssub LIKE '%a%'");
    while( db_step(&q)==SQLITE_ROW ){
      const char *zCode = db_column_text(&q, 1);
      zTo = db_column_text(&q, 0);
      blob_truncate(&hdr, 0);
      blob_appendf(&hdr, "To: %s\nSubject: %s %s\n", zTo, zSub, zSubject);
      if( zURL ){
        blob_truncate(&body, nUsed);
        blob_appendf(&body,"\n%.72c\nSubscription info: %s/alerts/%s\n",
           '-', zURL, zCode);
      }
      email_send(pSender, &hdr, &body);
    }
    db_finalize(&q);
  }
  if( bTest2 ){
    /* If the URL is /announce/test2 instead of just /announce, then no
    ** email is actually sent.  Instead, the text of the email that would
    ** have been sent is displayed in the result window. */
    @ <pre style='border: 2px solid blue; padding: 1ex'>
    @ %h(blob_str(&pSender->out))
    @ </pre>
  }
  zErr = pSender->zErr;
  pSender->zErr = 0;
  email_sender_free(pSender);
  return zErr;
}


/*
** WEBPAGE: announce
**
** A web-form, available to users with the "Send-Announcement" or "A"
** capability, that allows one to to send an announcements to whomever
** has subscribed to them.  The administrator can also send an announcement
** to the entire mailing list (including people who have elected to
** receive no announcements or notifications of any kind, or to
** individual email to anyone.
*/
void announce_page(void){
  const char *zTo = PT("to");
  login_check_credentials();
  if( !g.perm.Announce ){
    login_needed(0);
    return;
  }
  if( fossil_strcmp(P("name"),"test1")==0 ){
    /* Visit the /announce/test1 page to see the CGI variables */
    @ <p style='border: 1px solid black; padding: 1ex;'>
    cgi_print_all(0, 0);
    @ </p>
  }else
  if( P("submit")!=0 && cgi_csrf_safe(1) ){
    char *zErr = email_send_announcement();
    style_header("Announcement Sent");
    if( zErr ){
      @ <h1>Internal Error</h1>
      @ <p>The following error was reported by the system:
      @ <blockquote><pre>
      @ %h(zErr)
      @ </pre></blockquote>
    }else{
      @ <p>The announcement has been sent.</p>
    }
    style_footer();    
    return;
  }
  style_header("Send Announcement");
  @ <form method="POST">
  @ <table class="subscribe">
  if( g.perm.Admin ){
    int aa = PB("aa");
    int all = PB("all");
    const char *aack = aa ? "checked" : "";
    const char *allck = all ? "checked" : "";
    @ <tr>
    @  <td class="form_label">To:</td>
    @  <td><input type="text" name="to" value="%h(PT("to"))" size="30"><br>
    @  <label><input type="checkbox" name="aa" %s(aack)> \
    @  All "announcement" subscribers</label><br>
    @  <label><input type="checkbox" name="all" %s(allck)> \
    @  All subscribers</label></td>
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
  @   <td><input type="submit" name="submit" value="Send Message">
  @ </tr>
  @ </table>
  @ </form>
  style_footer();
}
