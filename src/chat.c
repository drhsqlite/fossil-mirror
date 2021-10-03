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

/*
** Outputs JS code to initialize a list of chat alert audio files for
** use by the chat front-end client. A handful of builtin files
** (from alerts/\*.wav) and all unversioned files matching
** alert-sounds/\*.{mp3,ogg,wav} are included.
*/
static void chat_emit_alert_list(void){
  unsigned int i;
  const char * azBuiltins[] = {
  "builtin/alerts/plunk.wav",
  "builtin/alerts/bflat2.wav",
  "builtin/alerts/bflat3.wav",
  "builtin/alerts/bloop.wav"
  };
  CX("window.fossil.config.chat.alerts = [\n");
  for(i=0; i < sizeof(azBuiltins)/sizeof(azBuiltins[0]); ++i){
    CX("%s%!j", i ? ", " : "", azBuiltins[i]);
  }
  if( db_table_exists("repository","unversioned") ){
    Stmt q = empty_Stmt;
    db_prepare(&q, "SELECT 'uv/'||name FROM unversioned "
               "WHERE content IS NOT NULL "
               "AND (name LIKE 'alert-sounds/%%.wav' "
               "OR name LIKE 'alert-sounds/%%.mp3' "
               "OR name LIKE 'alert-sounds/%%.ogg')");
    while(SQLITE_ROW==db_step(&q)){
      CX(", %!j", db_column_text(&q, 0));
    }
    db_finalize(&q);
  }
  CX("\n];\n");
}

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
** SETTING: chat-inline-images    boolean default=on
**
** Specifies whether posted images in /chat should default to being
** displayed inline or as downloadable links. Each chat user can
** change this value for their current chat session in the UI.
*/
/*
** SETTING: chat-poll-timeout    width=10 default=420
**
** On an HTTP request to /chat-poll, if there is no new content available,
** the reply is delayed waiting for new content to arrive.  (This is the
** "long poll" strategy of event delivery to the client.)  This setting
** determines approximately how long /chat-poll will delay before giving
** up and returning an empty reply.  The default value is about 7 minutes,
** which works well for Fossil behind the althttpd web server.  Other
** server environments may choose a longer or shorter delay.
**
** For maximum efficiency, it is best to choose the longest delay that
** does not cause timeouts in intermediate proxies or web server.
*/
/*
** SETTING: chat-alert-sound     width=10
**
** This is the name of the builtin sound file to use for the alert tone.
** The value must be the name of a builtin WAV file.
*/
/*
** WEBPAGE: chat
**
** Start up a browser-based chat session.
**
** This is the main page that humans use to access the chatroom.  Simply
** point a web-browser at /chat and the screen fills with the latest
** chat messages, and waits for new one.
**
** Other /chat-OP pages are used by XHR requests from this page to
** send new chat message, delete older messages, or poll for changes.
*/
void chat_webpage(void){
  char *zAlert;
  char *zProjectName;
  char * zInputPlaceholder0;  /* Common text input placeholder value */
  const char *zPaperclip =
    "<svg height=\"12.28288\" width=\"13.990313\"><path d=\"M "
    "12.266368,0.65287795 A 4.1,4.1 0 0 0 7.0963684,1.2528779 "
    "l -6.24999999,6.25 a 2.79,2.79 0 0 0 -0.84,2.19 2.84,2.84 "
    "0 0 0 1.13999999,2.0600001 2.73,2.73 0 0 0 1.62,0.53 3.13,"
    "3.13 0 0 0 2.18,-0.92 L 10.346368,5.9628779 a 1.66,1.66 0 "
    "0 0 -2.3399996,-2.35 l -3.6,3.6 0.71,0.71 3.59,-3.6 a 0.66,"
    "0.66 0 0 1 0.93,0.93 l -5.39,5.4000001 a 2,2 0 0 1 -2.5,0.3 "
    "1.83,1.83 0 0 1 -0.2,-2.7400001 l 6.25,-6.25 a 3.11,3.11 0 "
    "0 1 3.8999996,-0.48 3,3 0 0 1 1.31,2.19 3,3 0 0 1 -0.86,2.37 "
    "l -5.2799996,5.2600001 0.7,0.71 5.2699996,-5.2600001 a 4,4 0 "
    "0 0 -0.57,-6.09999995 z\" /></svg>";

  login_check_credentials();
  if( !g.perm.Chat ){
    login_needed(g.anon.Chat);
    return;
  }
  zAlert = mprintf("%s/builtin/%s", g.zBaseURL,
                db_get("chat-alert-sound","alerts/plunk.wav"));
  zProjectName = db_get("project-name","Unnamed project");
  zInputPlaceholder0 =
    mprintf("Type markdown-formatted message for %h.", zProjectName);
  style_set_current_feature("chat");
  style_header("Chat");
  @ <div id='chat-input-area'>
  @   <div id='chat-input-line' class='single-line'>
  @     <div contenteditable id="chat-input-field" \
  @      data-placeholder0="%h(zInputPlaceholder0)" \
  @      data-placeholder="%h(zInputPlaceholder0)" \
  @      class=""></div>
  @     <div id='chat-edit-buttons'>
  @       <button id="chat-preview-button" \
  @         title="Preview message (Shift-Enter)">&#128065;</button>
  @       <button id="chat-message-attach" \
  @         title="Attach file to message">%s(zPaperclip)</button>
  @       <button id="chat-settings-button" \
  @         title="Configure chat">&#9881;</button>
  @       <button id="chat-message-submit" \
  @         title="Send message (Ctrl-Enter)">&#128228;</button>
  @     </div>
  @   </div>
  @   <div id='chat-input-file-area'>
  @     <div class='file-selection-wrapper hidden'>
  @       <input type="file" name="file" id="chat-input-file">
  @     </div>
  @     <div id="chat-drop-details"></div>
  @   </div>
  @ </div>
  @ <div id='chat-user-list-wrapper' class='hidden'>
  @   <div class='legend'>
  @     <span class='help-buttonlet'>
  @      Users who have messages in the currently-loaded list.<br><br>
  @      <strong>Tap a user name</strong> to filter messages
  @      on that user and tap again to clear the filter.<br><br>
  @      <strong>Tap the title</strong> of this widget to toggle
  @      the list on and off.
  @     </span>
  @     <span>Active users (sorted by last message time)</span>
  @   </div>
  @   <div id='chat-user-list'></div>
  @ </div>
  @ <div id='chat-preview' class='hidden chat-view'>
  @  <header>Preview: (<a href='%R/md_rules' target='_blank'>markdown reference</a>)</header>
  @  <div id='chat-preview-content' class='message-widget-content'></div>
  @  <div id='chat-preview-buttons'><button id='chat-preview-close'>Close Preview</button></div>
  @ </div>
  @ <div id='chat-config' class='hidden chat-view'>
  @ <div id='chat-config-options'></div>
    /* ^^^populated client-side */
  @ <button>Close Settings</button>
  @ </div>
  @ <div id='chat-messages-wrapper' class='chat-view'>
  /* New chat messages get inserted immediately after this element */
  @ <span id='message-inject-point'></span>
  @ </div>
  fossil_free(zProjectName);
  fossil_free(zInputPlaceholder0);
  builtin_fossil_js_bundle_or("popupwidget", "storage", "fetch",
                              "pikchr", "confirmer", NULL);
  /* Always in-line the javascript for the chat page */
  @ <script nonce="%h(style_nonce())">/* chat.c:%d(__LINE__) */
  /* We need an onload handler to ensure that window.fossil is
     initialized before the chat init code runs. */
  @ window.addEventListener('load', function(){
  @ document.body.classList.add('chat');
  @ /*^^^for skins which add their own BODY tag */;
  @ window.fossil.config.chat = {
  @   fromcli: %h(PB("cli")?"true":"false"),
  @   alertSound: "%h(zAlert)",
  @   initSize: %d(db_get_int("chat-initial-history",50)),
  @   imagesInline: !!%d(db_get_boolean("chat-inline-images",1))
  @ };
  ajax_emit_js_preview_modes(0);
  chat_emit_alert_list();
  @ }, false);
  @ </script>
  builtin_request_js("fossil.page.chat.js");
  style_finish_page();
}

/* Definition of repository tables used by chat
*/
static const char zChatSchema1[] =
@ CREATE TABLE repository.chat(
@   msgid INTEGER PRIMARY KEY AUTOINCREMENT,
@   mtime JULIANDAY,  -- Time for this entry - Julianday Zulu
@   lmtime TEXT,      -- Client YYYY-MM-DDZHH:MM:SS when message originally sent
@   xfrom TEXT,       -- Login of the sender
@   xmsg  TEXT,       -- Raw, unformatted text of the message
@   fname TEXT,       -- Filename of the uploaded file, or NULL
@   fmime TEXT,       -- MIMEType of the upload file, or NULL
@   mdel INT,         -- msgid of another message to delete
@   file  BLOB        -- Text of the uploaded file, or NULL
@ );
;


/*
** Make sure the repository data tables used by chat exist.  Create them
** if they do not.
*/
static void chat_create_tables(void){
  if( !db_table_exists("repository","chat") ){
    db_multi_exec(zChatSchema1/*works-like:""*/);
  }else if( !db_table_has_column("repository","chat","lmtime") ){
    if( !db_table_has_column("repository","chat","mdel") ){
      db_multi_exec("ALTER TABLE chat ADD COLUMN mdel INT");
    }
    db_multi_exec("ALTER TABLE chat ADD COLUMN lmtime TEXT");
  }
}

/*
** Delete old content from the chat table.
*/
static void chat_purge(void){
   int mxCnt = db_get_int("chat-keep-count",50);
   double mxDays = atof(db_get("chat-keep-days","7"));
   double rAge;
   int msgid;
   rAge = db_double(0.0, "SELECT julianday('now')-mtime FROM chat"
                         " ORDER BY msgid LIMIT 1");
   if( rAge>mxDays ){
     msgid = db_int(0, "SELECT msgid FROM chat"
                       " ORDER BY msgid DESC LIMIT 1 OFFSET %d", mxCnt);
     if( msgid>0 ){
       Stmt s;
       db_multi_exec("PRAGMA secure_delete=ON;");
       db_prepare(&s, 
             "DELETE FROM chat WHERE mtime<julianday('now')-:mxage"
             " AND msgid<%d", msgid);
       db_bind_double(&s, ":mxage", mxDays);
       db_step(&s);
       db_finalize(&s);
     }
   }
}

/*
** Sets the current CGI response type to application/json then emits a
** JSON-format error message object. If fAsMessageList is true then
** the object is output using the list format described for chat-poll,
** else it is emitted as a single object in that same format.
*/
static void chat_emit_permissions_error(int fAsMessageList){
  char * zTime = cgi_iso8601_datestamp();
  cgi_set_content_type("application/json");
  if(fAsMessageList){
    CX("{\"msgs\":[{");
  }else{
    CX("{");
  }
  CX("\"isError\": true, \"xfrom\": null,");
  CX("\"mtime\": %!j, \"lmtime\": %!j,", zTime, zTime);
  CX("\"xmsg\": \"Missing permissions or not logged in. "
     "Try <a href='%R/login?g=chat'>logging in</a>.\"");
  if(fAsMessageList){
    CX("}]}");
  }else{
    CX("}");
  }
  fossil_free(zTime);
}

/*
** WEBPAGE: chat-send hidden
**
** This page receives (via XHR) a new chat-message and/or a new file
** to be entered into the chat history.
**
** On success it responds with an empty response: the new message
** should be fetched via /chat-poll. On error, e.g. login expiry,
** it emits a JSON response in the same form as described for
** /chat-poll errors, but as a standalone object instead of a
** list of objects.
*/
void chat_send_webpage(void){
  int nByte;
  const char *zMsg;
  const char *zUserName;
  login_check_credentials();
  if( !g.perm.Chat ) {
    chat_emit_permissions_error(0);
    return;
  }
  chat_create_tables();
  zUserName = (g.zLogin && g.zLogin[0]) ? g.zLogin : "nobody";
  nByte = atoi(PD("file:bytes","0"));
  zMsg = PD("msg","");
  db_begin_write();
  chat_purge();
  if( nByte==0 ){
    if( zMsg[0] ){
      db_multi_exec(
        "INSERT INTO chat(mtime,lmtime,xfrom,xmsg)"
        "VALUES(julianday('now'),%Q,%Q,%Q)",
        P("lmtime"), zUserName, zMsg
      );
    }
  }else{
    Stmt q;
    Blob b;
    db_prepare(&q,
        "INSERT INTO chat(mtime,lmtime,xfrom,xmsg,file,fname,fmime)"
        "VALUES(julianday('now'),%Q,%Q,%Q,:file,%Q,%Q)",
        P("lmtime"), zUserName, zMsg, PD("file:filename",""),
        PD("file:mimetype","application/octet-stream"));
    blob_init(&b, P("file"), nByte);
    db_bind_blob(&q, ":file", &b);
    db_step(&q);
    db_finalize(&q);
    blob_reset(&b);
  }
  db_commit_transaction();
}

/*
** This routine receives raw (user-entered) message text and transforms
** it into HTML that is safe to insert using innerHTML. As of 2021-09-19,
** it does so by using markdown_to_html() to convert markdown-formatted
** zMsg to HTML.
**
** Space to hold the returned string is obtained from fossil_malloc()
** and must be freed by the caller.
*/
static char *chat_format_to_html(const char *zMsg){
  Blob out;
  blob_init(&out, "", 0);
  if(*zMsg){
    Blob bIn;
    blob_init(&bIn, zMsg, (int)strlen(zMsg));
    markdown_to_html(&bIn, NULL, &out);
  }
  return blob_str(&out);
}

/*
** COMMAND: test-chat-formatter
**
** Usage: %fossil test-chat-formatter STRING ...
**
** Transform each argument string into HTML that will display the
** chat message.  This is used to test the formatter and to verify
** that a malicious message text will not cause HTML or JS injection
** into the chat display in a browser.
*/
void chat_test_formatter_cmd(void){
  int i;
  char *zOut;
  db_find_and_open_repository(0,0);
  g.perm.Hyperlink = 1;
  for(i=0; i<g.argc; i++){
    zOut = chat_format_to_html(g.argv[i]);
    fossil_print("[%d]: %s\n", i, zOut);
    fossil_free(zOut);
  }
}

/*
** WEBPAGE: chat-poll hidden
**
** The chat page generated by /chat using an XHR to this page to
** request new chat content.  A typical invocation is:
**
**     /chat-poll/N
**     /chat-poll?name=N
**
** The "name" argument should begin with an integer which is the largest
** "msgid" that the chat page currently holds. If newer content is
** available, this routine returns that content straight away. If no new
** content is available, this webpage blocks until the new content becomes
** available.  In this way, the system implements "hanging-GET" or "long-poll"
** style event notification. If no new content arrives after a delay of
** approximately chat-poll-timeout seconds (default: 420), then reply is
** sent with an empty "msg": field.
**
** If N is negative, then the return value is the N most recent messages.
** Hence a request like /chat-poll/-100 can be used to initialize a new
** chat session to just the most recent messages.
**
** Some webservers (althttpd) do not allow a term of the URL path to
** begin with "-".  Then /chat-poll/-100 cannot be used.  Instead you
** have to say "/chat-poll?name=-100".
**
** If the integer parameter "before" is passed in, it is assumed that
** the client is requesting older messages, up to (but not including)
** that message ID, in which case the next-oldest "n" messages
** (default=chat-initial-history setting, equivalent to n=0) are
** returned (negative n fetches all older entries). The client then
** needs to take care to inject them at the end of the history rather
** than the same place new messages go.
**
** If "before" is provided, "name" is ignored.
**
** The reply from this webpage is JSON that describes the new content.
** Format of the json:
**
** |    {
** |      "msgs":[
** |        {
** |           "msgid": integer // message id
** |           "mtime": text    // When sent: YYYY-MM-DDTHH:MM:SSZ
** |           "lmtime: text    // Sender's client-side YYYY-MM-DDTHH:MM:SS
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
**
** The "lmtime" value might be unknown, in which case it is omitted.
**
** The messages are ordered oldest first unless "before" is provided, in which
** case they are sorted newest first (to facilitate the client-side UI update).
**
** As a special case, if this routine encounters an error, e.g. the user's
** permissions cannot be verified because their login cookie expired, the
** request returns a slightly modified structure:
**
** |    {
** |      "msgs":[
** |        {
** |          "isError": true,
** |          "xfrom": null,
** |          "xmsg": "error details"
** |          "mtime": as above,
** |          "ltime": same as mtime
** |        }
** |      ]
** |    }
**
** If the client gets such a response, it should display the message
** in a prominent manner and then stop polling for new messages.
*/
void chat_poll_webpage(void){
  Blob json;                  /* The json to be constructed and returned */
  sqlite3_int64 dataVersion;  /* Data version.  Used for polling. */
  const int iDelay = 1000;    /* Delay until next poll (milliseconds) */
  int nDelay;                 /* Maximum delay.*/
  int msgid = atoi(PD("name","0"));
  const int msgBefore = atoi(PD("before","0"));
  int nLimit = msgBefore>0 ? atoi(PD("n","0")) : 0;
  Blob sql = empty_blob;
  Stmt q1;
  nDelay = db_get_int("chat-poll-timeout",420);  /* Default about 7 minutes */
  login_check_credentials();
  if( !g.perm.Chat ) {
    chat_emit_permissions_error(1);
    return;
  }
  chat_create_tables();
  cgi_set_content_type("application/json");
  dataVersion = db_int64(0, "PRAGMA data_version");
  blob_append_sql(&sql,
    "SELECT msgid, datetime(mtime), xfrom, xmsg, length(file),"
    "       fname, fmime, %s, lmtime"
    "  FROM chat ",
    msgBefore>0 ? "0 as mdel" : "mdel");
  if( msgid<=0 || msgBefore>0 ){
    db_begin_write();
    chat_purge();
    db_commit_transaction();
  }
  if(msgBefore>0){
    if(0==nLimit){
      nLimit = db_get_int("chat-initial-history",50);
    }
    blob_append_sql(&sql,
      " WHERE msgid<%d"
      " ORDER BY msgid DESC "
      "LIMIT %d",
      msgBefore, nLimit>0 ? nLimit : -1
    );
  }else{
    if( msgid<0 ){
      msgid = db_int(0,
            "SELECT msgid FROM chat WHERE mdel IS NOT true"
            " ORDER BY msgid DESC LIMIT 1 OFFSET %d", -msgid);
    }
    blob_append_sql(&sql,
      " WHERE msgid>%d"
      " ORDER BY msgid",
      msgid
    );
  }
  db_prepare(&q1, "%s", blob_sql_text(&sql));
  blob_reset(&sql);
  blob_init(&json, "{\"msgs\":[\n", -1);
  while( nDelay>0 ){
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
      const char *zLMtime = db_column_text(&q1, 8);
      char *zMsg;
      if(cnt++){
        blob_append(&json, ",\n", 2);
      }
      blob_appendf(&json, "{\"msgid\":%d,", id);
      blob_appendf(&json, "\"mtime\":\"%.10sT%sZ\",", zDate, zDate+11);
      if( zLMtime && zLMtime[0] ){
        blob_appendf(&json, "\"lmtime\":%!j,", zLMtime);
      }
      blob_append(&json, "\"xfrom\":", -1);
      if(zFrom){
        blob_appendf(&json, "%!j,", zFrom);
      }else{
        /* see https://fossil-scm.org/forum/forumpost/e0be0eeb4c */
        blob_appendf(&json, "null,");
      }
      blob_appendf(&json, "\"uclr\":%!j,",
                   user_color(zFrom ? zFrom : "nobody"));

      zMsg = chat_format_to_html(zRawMsg ? zRawMsg : "");
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
    if( cnt || msgBefore>0 ){
      break;
    }
    sqlite3_sleep(iDelay); nDelay--;
    while( nDelay>0 ){
      sqlite3_int64 newDataVers = db_int64(0,"PRAGMA repository.data_version");
      if( newDataVers!=dataVersion ){
        dataVersion = newDataVers;
        break;
      }
      sqlite3_sleep(iDelay); nDelay--;
    }
  } /* Exit by "break" */
  db_finalize(&q1);
  blob_append(&json, "\n]}", 3);
  cgi_set_content(&json);
  return;      
}

/*
** WEBPAGE: chat-fetch-one hidden
**
** /chat-fetch-one/N
**
** Fetches a single message with the given ID, if available.
**
** Options:
**
**   raw = the xmsg field will be returned unparsed.
**
** Response is either a single object in the format returned by
** /chat-poll (without the wrapper array) or a JSON-format error
** response, as documented for ajax_route_error().
*/
void chat_fetch_one(void){
  Blob json = empty_blob;   /* The json to be constructed and returned */
  const int fRaw = PD("raw",0)!=0;
  const int msgid = atoi(PD("name","0"));
  Stmt q;
  login_check_credentials();
  if( !g.perm.Chat ) {
    chat_emit_permissions_error(0);
    return;
  }
  chat_create_tables();
  cgi_set_content_type("application/json");
  db_prepare(&q, 
    "SELECT datetime(mtime), xfrom, xmsg, length(file),"
    "       fname, fmime, lmtime"
    "  FROM chat WHERE msgid=%d AND mdel IS NULL",
    msgid);
  if(SQLITE_ROW==db_step(&q)){
    const char *zDate = db_column_text(&q, 0);
    const char *zFrom = db_column_text(&q, 1);
    const char *zRawMsg = db_column_text(&q, 2);
    const int nByte = db_column_int(&q, 3);
    const char *zFName = db_column_text(&q, 4);
    const char *zFMime = db_column_text(&q, 5);
    const char *zLMtime = db_column_text(&q, 7);
    blob_appendf(&json,"{\"msgid\": %d,", msgid);

    blob_appendf(&json, "\"mtime\":\"%.10sT%sZ\",", zDate, zDate+11);
    if( zLMtime && zLMtime[0] ){
      blob_appendf(&json, "\"lmtime\":%!j,", zLMtime);
    }
    blob_append(&json, "\"xfrom\":", -1);
    if(zFrom){
      blob_appendf(&json, "%!j,", zFrom);
    }else{
      /* see https://fossil-scm.org/forum/forumpost/e0be0eeb4c */
      blob_appendf(&json, "null,");
    }
    blob_appendf(&json, "\"uclr\":%!j,",
                 user_color(zFrom ? zFrom : "nobody"));
    blob_append(&json,"\"xmsg\":", 7);
    if(fRaw){
      blob_appendf(&json, "%!j,", zRawMsg);
    }else{
      char * zMsg = chat_format_to_html(zRawMsg ? zRawMsg : "");
      blob_appendf(&json, "%!j,", zMsg);
      fossil_free(zMsg);
    }
    if( nByte==0 ){
      blob_appendf(&json, "\"fsize\":0");
    }else{
      blob_appendf(&json, "\"fsize\":%d,\"fname\":%!j,\"fmime\":%!j",
                   nByte, zFName, zFMime);
    }    
    blob_append(&json,"}",1);
    cgi_set_content(&json);
  }else{
    ajax_route_error(404,"Chat message #%d not found.", msgid);
  }
  db_finalize(&q);
}

/*
** WEBPAGE: chat-download hidden
**
** Download the CHAT.FILE attachment associated with a single chat
** entry.  The "name" query parameter begins with an integer that
** identifies the particular chat message. The integer may be followed
** by a / and a filename, which will indicate to the browser to use
** the indicated name when saving the file.
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
** WEBPAGE: chat-delete hidden
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
    "PRAGMA secure_delete=ON;\n"
    "BEGIN;\n"
    "DELETE FROM chat WHERE msgid=%d;\n"
    "INSERT INTO chat(mtime, xfrom, mdel)"
    " VALUES(julianday('now'), %Q, %d);\n"
    "COMMIT;",
    mdel, g.zLogin, mdel
  );
}

/*
** COMMAND: chat
**
** Usage: %fossil chat [SUBCOMMAND] [--remote URL] [ARGS...]
**
** This command performs actions associated with the /chat instance
** on the default remote Fossil repository (the Fossil repository whose
** URL shows when you run the "fossil remote" command) or to the URL
** specified by the --remote option.  If there is no default remote
** Fossil repository and the --remote option is omitted, then this
** command fails with an error.
**
** When there is no SUBCOMMAND (when this command is simply "fossil chat")
** the response is to bring up a web-browser window to the chatroom
** on the default system web-browser.  You can accomplish the same by
** typing the appropriate URL into the web-browser yourself.  This
** command is merely a convenience for command-line oriented people.
**
** The following subcommands are supported:
**
** > fossil chat send [ARGUMENTS]
**
**      This command sends a new message to the chatroom.  The message
**      to be sent is determined by arguments as follows:
**
**        -f|--file FILENAME     File to attach to the message
**        -m|--message TEXT      Text of the chat message
**        --unsafe               Allow the use of unencrypted http://
**
** Additional subcommands may be added in the future.
*/
void chat_command(void){
  const char *zUrl = find_option("remote",0,1);
  int urlFlags = 0;
  int isDefaultUrl = 0;
  int i;

  db_find_and_open_repository(0,0);
  if( zUrl ){
    urlFlags = URL_PROMPT_PW;
  }else{
    zUrl = db_get("last-sync-url",0);
    if( zUrl==0 ){
      fossil_fatal("no \"remote\" repository defined");
    }else{
      isDefaultUrl = 1;
    }
  }
  url_parse(zUrl, urlFlags);
  if( g.url.isFile || g.url.isSsh ){
    fossil_fatal("chat only works for http:// and https:// URLs");
  }
  i = (int)strlen(g.url.path);
  while( i>0 && g.url.path[i-1]=='/' ) i--;
  if( g.url.port==g.url.dfltPort ){
    zUrl = mprintf(
      "%s://%T%.*T",
      g.url.protocol, g.url.name, i, g.url.path
    );
  }else{
    zUrl = mprintf(
      "%s://%T:%d%.*T",
      g.url.protocol, g.url.name, g.url.port, i, g.url.path
    );
  }
  if( g.argc==2 ){
    const char *zBrowser = fossil_web_browser();
    char *zCmd;
    verify_all_options();
    if( zBrowser==0 ) return;
#ifdef _WIN32
    zCmd = mprintf("%s %s/chat?cli &", zBrowser, zUrl);
#else
    zCmd = mprintf("%s \"%s/chat?cli\" &", zBrowser, zUrl);
#endif
    fossil_system(zCmd);
  }else if( strcmp(g.argv[2],"send")==0 ){
    const char *zFilename = find_option("file","r",1);
    const char *zMsg = find_option("message","m",1);
    int allowUnsafe = find_option("unsafe",0,0)!=0;
    const int mFlags = HTTP_GENERIC | HTTP_QUIET | HTTP_NOCOMPRESS;
    int i;
    const char *zPw;
    char *zLMTime;
    Blob up, down, fcontent;
    char zBoundary[80];
    sqlite3_uint64 r[3];
    if( zFilename==0 && zMsg==0 ){
      fossil_fatal("must have --message or --file or both");
    }
    if( !g.url.isHttps && !allowUnsafe ){
      fossil_fatal("URL \"%s\" is unencrypted. Use https:// instead", zUrl);
    }
    verify_all_options();
    if( g.argc>3 ){
      fossil_fatal("unknown extra argument: \"%s\"", g.argv[3]);
    }
    i = (int)strlen(g.url.path);
    while( i>0 && g.url.path[i-1]=='/' ) i--;
    g.url.path = mprintf("%.*s/chat-send", i, g.url.path);
    blob_init(&up, 0, 0);
    blob_init(&down, 0, 0);
    sqlite3_randomness(sizeof(r),r);
    sqlite3_snprintf(sizeof(zBoundary),zBoundary,
                     "--------%016llu%016llu%016llu", r[0], r[1], r[2]);
    blob_appendf(&up, "%s", zBoundary);
    zLMTime = db_text(0,
              "SELECT strftime('%%Y-%%m-%%dT%%H:%%M:%%S','now','localtime')");
    if( zLMTime ){
      blob_appendf(&up,"\r\nContent-Disposition: form-data; name=\"lmtime\"\r\n"
                       "\r\n%z\r\n%s", zLMTime, zBoundary);
    }
    if( g.url.user && g.url.user[0] ){
      blob_appendf(&up,"\r\nContent-Disposition: form-data; name=\"resid\"\r\n"
                       "\r\n%z\r\n%s", obscure(g.url.user), zBoundary);
    }
    zPw = g.url.passwd;
    if( zPw==0 && isDefaultUrl ) zPw = unobscure(db_get("last-sync-pw", 0));
    if( zPw && zPw[0] ){
      blob_appendf(&up,"\r\nContent-Disposition: form-data; name=\"token\"\r\n"
                       "\r\n%z\r\n%s", obscure(zPw), zBoundary);
    }
    if( zMsg && zMsg[0] ){
      blob_appendf(&up,"\r\nContent-Disposition: form-data; name=\"msg\"\r\n"
                       "\r\n%s\r\n%s", zMsg, zBoundary);
    }
    if( zFilename && blob_read_from_file(&fcontent, zFilename, ExtFILE)>0 ){
      char *zFN = mprintf("%s", file_tail(zFilename));
      int i;
      const char *zMime = mimetype_from_name(zFilename);
      for(i=0; zFN[i]; i++){
        char c = zFN[i];
        if( fossil_isalnum(c) ) continue;
        if( c=='.' ) continue;
        if( c=='-' ) continue;
        zFN[i] = '_';
      }
      blob_appendf(&up,"\r\nContent-Disposition: form-data; name=\"file\";"
                       " filename=\"%s\"\r\n", zFN);
      blob_appendf(&up,"Content-Type: %s\r\n\r\n", zMime);
      blob_append(&up, fcontent.aData, fcontent.nUsed);
      blob_appendf(&up,"\r\n%s", zBoundary);
    }
    blob_append(&up,"--\r\n", 4);
    http_exchange(&up, &down, mFlags, 4, "multipart/form-data");
    blob_reset(&up);
    if( sqlite3_strglob("{\"isError\": true,*", blob_str(&down))==0 ){
      if( strstr(blob_str(&down), "not logged in")!=0 ){
        fossil_print("ERROR: username and/or password is incorrect\n");
      }else{
        fossil_print("ERROR: %s\n", blob_str(&down));
      }
      fossil_fatal("unable to send the chat message");
    }
    blob_reset(&down);
  }else if( strcmp(g.argv[2],"url")==0 ){
    /* Undocumented command.  Show the URL to access chat. */
    fossil_print("%s/chat\n", zUrl);
  }else{
    fossil_fatal("no such subcommand \"%s\".  Use --help for help", g.argv[2]);
  }
}
