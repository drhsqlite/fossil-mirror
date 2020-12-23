/*
** Copyright (c) 2020 D. Richard Hipp
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
** This file contains code used to implement the Fossil chatroom.
**
** Initial design goals:
**
**    *   Keep it simple.  This chatroom is not intended as a competitor
**        or replacement for IRC, Discord, Telegram, Slack, etc.  The goal
**        is zero- or near-zero-configuration, not an abundance of features.
**
**    *   Intended as a place for insiders to have ephemeral conversations
**        about a project.  This is not a public gather place.  Think
**        "boardroom", not "corner pub".
**
**    *   One chatroom per repository.
**
**    *   Chat content lives in a single repository.  It is never synced.
**        Content expires and is deleted after a set interval (a week or so).
**
** Notification is accomplished using the "hanging GET" or "long poll" design
** in which a GET request is issued but the server does not send a reply until
** new content arrives.  Newer Web Sockets and Server Sent Event protocols are
** more elegant, but are not compatible with CGI, and would thus complicate
** configuration.  
*/
#include "config.h"
#include <assert.h>
#include "chat.h"

/* Settings that can be used to control chat */
/*
** SETTING: chat-initial-history    width=10 default=50
**
** If this setting has an integer value of N, then when /chat first
** starts up it initializes the screen with the N most recent chat
** messages.  If N is zero, then all chat messages are loaded.
*/
/*
** SETTING: chat-keep-count    width=10 default=50
**
** When /chat is cleaning up older messages, it will always keep
** the most recent chat-keep-count messages, even if some of those
** messages are older than the discard threshold.  If this value
** is zero, then /chat is free to delete all historic messages once
** they are old enough.
*/
/*
** SETTING: chat-keep-days    width=10 default=7
**
** The /chat subsystem will try to discard messages that are older then
** chat-keep-days.  The value of chat-keep-days can be a floating point
** number.  So, for example, if you only want to keep chat messages for
** 12 hours, set this value to 0.5.
**
** A value of 0.0 or less means that messages are retained forever.
*/

/*
** WEBPAGE: chat
**
** Start up a browser-based chat session.
*/
void chat_webpage(void){
  login_check_credentials();
  style_set_current_feature("chat");
  if( !g.perm.Chat ){
    style_header("Chat Not Authorized");
    @ <h1>Not Authorized</h1>
    @ <p>You do not have permission to use the chatroom on this
    @ repository.</p>
    style_finish_page();
    return;
  }
  style_header("Chat");
  @ <style>
  @ #dialog {
  @  width: 97%%;
  @ }
  @ #chat-input-area {
  @   width: 100%%;
  @   display: flex;
  @   flex-direction: column;
  @ }
  @ #chat-input-line {
  @   display: flex;
  @   flex-direction: row;
  @   margin-bottom: 1em;
  @   align-items: center;
  @ }
  @ #chat-input-line > input[type=submit] {
  @   flex: 1 5 auto;
  @   max-width: 6em;
  @ }
  @ #chat-input-line > input[type=text] {
  @   flex: 5 1 auto;
  @ }
  @ #chat-input-file-area  {
  @   display: flex;
  @   flex-direction: row;
  @   align-items: center;
  @ }
  @ #chat-input-file-area > .help-buttonlet,
  @ #chat-input-file {
  @   align-self: flex-start;
  @   margin-right: 0.5em;
  @   flex: 0 1 auto;
  @ }
  @ #chat-input-file {
  @   border:1px solid rgba(0,0,0,0);/*avoid UI shift during drop-targeting*/
  @   border-radius: 0.25em;
  @ }
  @ #chat-input-file > input {
  @   flex: 1 0 auto;
  @ }
  @ .chat-timestamp {
  @    font-family: monospace;
  @    font-size: 0.8em;
  @    white-space: pre;
  @    text-align: left;
  @    opacity: 0.8;
  @ }
  @ #chat-input-file.dragover {
  @   border: 1px dashed green;
  @ }
  @ #chat-drop-details {
  @   flex: 0 1 auto;
  @   padding: 0.5em 1em;
  @   margin-left: 0.5em;
  @   white-space: pre;
  @   font-family: monospace;
  @   max-width: 50%%;
  @ }
  @ </style>
  @ <form accept-encoding="utf-8" id="chat-form" autocomplete="off">
  @ <div id='chat-input-area'>
  @   <div id='chat-input-line'>
  @     <input type="text" name="msg" id="sbox" \
  @      placeholder="Type message here.">
  @     <input type="submit" value="Send">
  @   </div>
  @   <div id='chat-input-file-area'>
  @     <input type="file" name="file" id="chat-input-file">
  @     <div id="chat-drop-details"></div>
  @   </div>
  @ </div>
  @ </form>
  @ <hr>

  /* New chat messages get inserted immediately after this element */
  @ <span id='message-inject-point'></span>

  builtin_fossil_js_bundle_or("popupwidget", NULL);
  /* Always in-line the javascript for the chat page */
  @ <script nonce="%h(style_nonce())">/* chat.c:%d(__LINE__) */
  @ window.fossilChatInitSize = %d(db_get_int("chat-initial-history",50));
  @ window.addEventListener('load', function(){
  /* We need an onload handler to ensure that window.fossil is
     loaded first. */
  cgi_append_content(builtin_text("chat.js"),-1);
  @ }, false);
  @ </script>

  style_finish_page();
}

/* Definition of repository tables used by chat
*/
static const char zChatSchema1[] =
@ CREATE TABLE repository.chat(
@   msgid INTEGER PRIMARY KEY AUTOINCREMENT,
@   mtime JULIANDAY,       -- Time for this entry - Julianday Zulu
@   xfrom TEXT,            -- Login of the sender
@   xmsg  TEXT,            -- Raw, unformatted text of the message
@   file  BLOB,            -- Text of the uploaded file, or NULL
@   fname TEXT,            -- Filename of the uploaded file, or NULL
@   fmime TEXT,            -- MIMEType of the upload file, or NULL
@   mdel INT               -- msgid of another message to delete
@ );
;


/*
** Make sure the repository data tables used by chat exist.  Create them
** if they do not.
*/
static void chat_create_tables(void){
  if( !db_table_exists("repository","chat") ){
    db_multi_exec(zChatSchema1/*works-like:""*/);
  }else if( !db_table_has_column("repository","chat","mdel") ){
    db_multi_exec("ALTER TABLE chat ADD COLUMN mdel INT");
  }
}

/*
** WEBPAGE: chat-send
**
** This page receives (via XHR) a new chat-message and/or a new file
** to be entered into the chat history.
*/
void chat_send_webpage(void){
  int nByte;
  const char *zMsg;
  login_check_credentials();
  if( !g.perm.Chat ) return;
  chat_create_tables();
  nByte = atoi(PD("file:bytes",0));
  zMsg = PD("msg","");
  if( nByte==0 ){
    if( zMsg[0] ){
      db_multi_exec(
        "INSERT INTO chat(mtime,xfrom,xmsg)"
        "VALUES(julianday('now'),%Q,%Q)",
        g.zLogin, zMsg
      );
    }
  }else{
    Stmt q;
    Blob b;
    db_prepare(&q,
        "INSERT INTO chat(mtime, xfrom,xmsg,file,fname,fmime)"
        "VALUES(julianday('now'),%Q,%Q,:file,%Q,%Q)",
        g.zLogin, zMsg, PD("file:filename",""),
        PD("file:mimetype","application/octet-stream"));
    blob_init(&b, P("file"), nByte);
    db_bind_blob(&q, ":file", &b);
    db_step(&q);
    db_finalize(&q);
    blob_reset(&b);
  }
}

/*
** WEBPAGE: chat-poll
**
** The chat page generated by /chat using a XHR to this page in order
** to ask for new chat content.  The "name" argument should begin with
** an integer which is the largest "msgid" that the chat page currently
** holds.  If newer content is available, this routine returns that
** content straight away.  If no new content is available, this webpage
** blocks until the new content becomes available.  In this way, the
** system implements "hanging-GET" or "long-poll" style event notification.
**
**      /chat-poll/N
**
** If N is negative, then the return value is the N most recent messages.
** Hence a request like /chat-poll/-100 can be used to initialize a new
** chat session to just the most recent messages.
**
** Some webservers (althttpd) do not allow a term of the URL path to
** begin with "-".  Then /chat-poll/-100 cannot be used.  Instead you
** have to say "/chat-poll?name=-100".
**
** The reply from this webpage is JSON that describes the new content.
** Format of the json:
**
** |    {
** |      "msg":[
** |        {
** |           "msgid": integer // message id
** |           "mtime": text    // When sent:  YYYY-MM-DD HH:MM:SS UTC
** |           "xfrom": text    // Login name of sender
** |           "uclr":  text    // Color string associated with the user
** |           "xmsg":  text    // HTML text of the message
** |           "fsize": integer // file attachment size in bytes
** |           "fname": text    // Name of file attachment
** |           "fmime": text    // MIME-type of file attachment
** |           "mdel":  integer // message id of prior message to delete
** |        }
** |      ]
** |    }
**
** The "fname" and "fmime" fields are only present if "fsize" is greater
** than zero.  The "xmsg" field may be an empty string if "fsize" is zero.
**
** The "msgid" values will be in increasing order.
**
** The "mdel" will only exist if "xmsg" is an empty string and "fsize" is zero.
*/
void chat_poll_webpage(void){
  Blob json;                  /* The json to be constructed and returned */
  sqlite3_int64 dataVersion;  /* Data version.  Used for polling. */
  int iDelay = 1000;          /* Delay until next poll (milliseconds) */
  const char *zSep = "{\"msgs\":[\n";   /* List separator */
  int msgid = atoi(PD("name","0"));
  Stmt q1;
  login_check_credentials();
  if( !g.perm.Chat ) return;
  chat_create_tables();
  cgi_set_content_type("text/json");
  dataVersion = db_int64(0, "PRAGMA data_version");
  if( msgid<0 ){
    msgid = db_int(0,
        "SELECT msgid FROM chat WHERE mdel IS NOT true"
        " ORDER BY msgid DESC LIMIT 1 OFFSET %d", -msgid);
  }
  db_prepare(&q1,
    "SELECT msgid, datetime(mtime), xfrom, xmsg, length(file),"
    "       fname, fmime, mdel"
    "  FROM chat"
    " WHERE msgid>%d"
    " ORDER BY msgid",
    msgid
  );
  blob_init(&json, 0, 0);
  while(1){
    int cnt = 0;
    while( db_step(&q1)==SQLITE_ROW ){
      int id = db_column_int(&q1, 0);
      const char *zDate = db_column_text(&q1, 1);
      const char *zFrom = db_column_text(&q1, 2);
      const char *zRawMsg = db_column_text(&q1, 3);
      int nByte = db_column_int(&q1, 4);
      const char *zFName = db_column_text(&q1, 5);
      const char *zFMime = db_column_text(&q1, 6);
      int iToDel = db_column_int(&q1, 7);
      char *zMsg;
      cnt++;
      blob_append(&json, zSep, -1);
      zSep = ",\n";
      blob_appendf(&json, "{\"msgid\":%d,\"mtime\":%!j,", id, zDate);
      blob_appendf(&json, "\"xfrom\":%!j,", zFrom);
      blob_appendf(&json, "\"uclr\":%!j,", hash_color(zFrom));

      /* TBD:  Convert the raw message into HTML, perhaps by running it
      ** through a text formatter, or putting markup on @name phrases,
      ** etc. */
      zMsg = mprintf("%h", zRawMsg ? zRawMsg : "");
      blob_appendf(&json, "\"xmsg\":%!j,", zMsg);
      fossil_free(zMsg);

      if( nByte==0 ){
        blob_appendf(&json, "\"fsize\":0");
      }else{
        blob_appendf(&json, "\"fsize\":%d,\"fname\":%!j,\"fmime\":%!j",
               nByte, zFName, zFMime);
      }
      if( iToDel ){
        blob_appendf(&json, ",\"mdel\":%d}", iToDel);
      }else{
        blob_append(&json, "}", 1);
      }
    }
    db_reset(&q1);
    if( cnt ){
      blob_append(&json, "\n]}", 3);
      cgi_set_content(&json);
      break;
    }
    sqlite3_sleep(iDelay);
    while( 1 ){
      sqlite3_int64 newDataVers = db_int64(0,"PRAGMA repository.data_version");
      if( newDataVers!=dataVersion ){
        dataVersion = newDataVers;
        break;
      }
      sqlite3_sleep(iDelay);
    }
  } /* Exit by "break" */
  db_finalize(&q1);
  return;      
}

/*
** WEBPAGE: chat-download
**
** Download the CHAT.FILE attachment associated with a single chat
** entry.  The "name" query parameter begins with an integer that
** identifies the particular chat message.
*/
void chat_download_webpage(void){
  int msgid;
  Blob r;
  const char *zMime;
  login_check_credentials();
  if( !g.perm.Chat ){
    style_header("Chat Not Authorized");
    @ <h1>Not Authorized</h1>
    @ <p>You do not have permission to use the chatroom on this
    @ repository.</p>
    style_finish_page();
    return;
  }
  chat_create_tables();
  msgid = atoi(PD("name","0"));
  blob_zero(&r);
  zMime = db_text(0, "SELECT fmime FROM chat wHERE msgid=%d", msgid);
  if( zMime==0 ) return;
  db_blob(&r, "SELECT file FROM chat WHERE msgid=%d", msgid);
  cgi_set_content_type(zMime);
  cgi_set_content(&r);
}


/*
** WEBPAGE: chat-delete
**
** Delete the chat entry identified by the name query parameter.
** Invoking fetch("chat-delete/"+msgid) from javascript in the client
** will delete a chat entry from the CHAT table.
**
** This routine both deletes the identified chat entry and also inserts
** a new entry with the current timestamp and with:
**
**   *  xmsg = NULL
**   *  file = NULL
**   *  mdel = The msgid of the row that was deleted
**
** This new entry will then be propagated to all listeners so that they
** will know to delete their copies of the message too.
*/
void chat_delete_webpage(void){
  int mdel;
  char *zOwner;
  login_check_credentials();
  if( !g.perm.Chat ) return;
  chat_create_tables();
  mdel = atoi(PD("name","0"));
  zOwner = db_text(0, "SELECT xfrom FROM chat WHERE msgid=%d", mdel);
  if( zOwner==0 ) return;
  if( fossil_strcmp(zOwner, g.zLogin)!=0 && !g.perm.Admin ) return;
  db_multi_exec(
    "BEGIN;\n"
    "DELETE FROM chat WHERE msgid=%d;\n"
    "INSERT INTO chat(mtime, xfrom, mdel)"
    " VALUES(julianday('now'), %Q, %d);\n"
    "COMMIT;",
    mdel, g.zLogin, mdel
  );
}
