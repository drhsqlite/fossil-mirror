#!/usr/bin/wapptclsh
#
# A chat program designed to run using the extcgi mechanism of Fossil.
#
encoding system utf-8

# The name of the chat database file
#
proc chat-db-name {} {
  set x [wapp-param SCRIPT_FILENAME]
  set dir [file dir $x]
  set fn [file tail $x]
  return $dir/-$fn.db
}

# Verify permission to use chat.  Return true if not authorized.
# Return false if the Fossil user is allowed to access chat.
#
proc not-authorized {} {
  set cap [wapp-param FOSSIL_CAPABILITIES]
  return [expr {![string match *i* $cap]}]
}

# The default page.
# Load the initial chat screen.
#
proc wapp-default {} {
  wapp-content-security-policy off
  wapp-trim {
    <div class="fossil-doc" data-title="Chat">
  }
  if {[not-authorized]} {
    wapp-trim {
      <h1>Not authorized</h1>
      <p>You must have privileges to use this chatroom</p>
      </div>
    }
    return
  }
  wapp-trim {
    <form accept-encoding="utf-8" id="chat-form">
    <div id='chat-input-area'>
      <div id='chat-input-line'>
        <input type="text" name="msg" id="sbox" placeholder="Type message here.">
        <input type="submit" value="Send">
      </div>
      <div id='chat-input-file'>
        <span>File:</span>
        <input type="file" name="file">
      </div>
    </div>
    </form>

    <hr>
    <table id="dialog">
    </table>
    </div>
    <hr>
    <p>
    <a href="chat/env">CGI environment</a> |
    <a href="chat/self">Wapp script</a>
    <style>
.chat-message {/*style common to .chat-mx and .chat-ms*/
  padding: 0.5em;
  border-radius: 0.5em;
  border: 1px solid black;
  box-shadow: 0.2em 0.2em 0.2em rgba(0, 0, 0, 0.29);
  /* Bob Ross might approve of... */
  /*
  background-image: linear-gradient(45deg, #FFC107 0%, #ff8b5f 100%);
  border-bottom: solid 3px #c58668;
  */
}
.chat-mx {
  float: left;
  margin-right: 3em;
}
.chat-ms {
  float: right;
  margin-left: 3em;
  background-color: #d2dde1;
}
\#dialog {
  width: 97%;
}
\#chat-input-area {
  width: 100%;
  display: flex;
  flex-direction: column;
}
\#chat-input-line {
  display: flex;
  flex-direction: row;
  margin-bottom: 1em;
  align-items: center;
}
\#chat-input-line > input[type=submit] {
  flex: 1 5 auto;
  max-width: 6em;
}
\#chat-input-line > input[type=text] {
  flex: 5 1 auto;
}
\#chat-input-file {
  display: flex;
  flex-direction: row;
  align-items: center;
}
\#chat-input-file > input {
  flex: 1 0 auto;
}
span.at-name { /* for @USERNAME references */
  text-decoration: underline;
  font-weight: bold;
}
</style>
  }
  set nonce [wapp-param FOSSIL_NONCE]
  set submiturl [wapp-param SCRIPT_NAME]/send
  set pollurl [wapp-param SCRIPT_NAME]/poll
  set downloadurl [wapp-param SCRIPT_NAME]/download
  set me [wapp-param FOSSIL_USER]
  wapp-trim {
<script nonce="%string($nonce)">
(function(){
  const form = document.querySelector('#chat-form');
  let mxMsg = 0;
  let _me = "%string($me)";
  form.addEventListener('submit',(e)=>{
    e.preventDefault();
    if( form.msg.value.length>0 || form.file.value.length>0 ){
      fetch("%string($submiturl)",{
        method: 'POST',
        body: new FormData(form)
      });
    }
    form.msg.value = "";
    form.file.value = "";
    form.msg.focus();
  });
  const rxUrl = /\\b(?:https?|ftp):\\/\\/\[a-z0-9-+&@\#\\/%?=~_|!:,.;]*\[a-z0-9-+&@\#\\/%=~_|]/gim;
  const rxAtName = /@\\w+/gmi;
  // ^^^ achtung, extra backslashes needed for the outer TCL.
  // Converts a message string to a message-containing DOM element
  // and returns that element, which may contain child elements.
  // If 2nd arg is passed, it must be a DOM element to which all
  // child elements are appended.
  const messageToDOM = function f(str, tgtElem){
    "use strict";
    if(!f.rxUrl){
      f.rxUrl = rxUrl;
      f.rxAt = rxAtName;
      f.rxNS = /\\S/;
      f.ce = (T)=>document.createElement(T);
      f.ct = (T)=>document.createTextNode(T);
      f.replaceUrls = function ff(sub, offset, whole){
        if(offset > ff.prevStart){
          f.accum.push((ff.prevStart?' ':'')+whole.substring(ff.prevStart, offset-1)+' ');
        }
        const a = f.ce('a');
        a.setAttribute('href',sub);
        a.setAttribute('target','_blank');
        a.appendChild(f.ct(sub));
        f.accum.push(a);
        ff.prevStart = offset + sub.length + 1;
      };
      f.replaceAtName = function ff(sub, offset,whole){
        if(offset > ff.prevStart){
          ff.accum.push((ff.prevStart?' ':'')+whole.substring(ff.prevStart, offset-1)+' ');
        }else if(offset && f.rxNS.test(whole[offset-1])){
          // Sigh: https://stackoverflow.com/questions/52655367
          ff.accum.push(sub);
          return;
        }
        const e = f.ce('span');
        e.classList.add('at-name');
        e.appendChild(f.ct(sub));
        ff.accum.push(e);
        ff.prevStart = offset + sub.length + 1;
      };
    }
    f.accum = []; // accumulate strings and DOM elements here.
    f.rxUrl.lastIndex = f.replaceUrls.prevStart = 0; // reset regex cursor
    str.replace(f.rxUrl, f.replaceUrls);
    // Push remaining non-URL part of the string to the queue...
    if(f.replaceUrls.prevStart < str.length){
      f.accum.push((f.replaceUrls.prevStart?' ':'')+str.substring(f.replaceUrls.prevStart));
    }
    // Pass 2: process @NAME references...
    // TODO: only match NAME if it's the name of a currently participating
    // user. Add a second class if NAME == current user, and style that one
    // differently so that people can more easily see when they're spoken to.
    const accum2 = f.replaceAtName.accum = [];
    //console.debug("f.accum =",f.accum);
    f.accum.forEach(function(v){
      //console.debug("v =",v);
      if('string'===typeof v){
        f.rxAt.lastIndex = f.replaceAtName.prevStart = 0;
        v.replace(f.rxAt, f.replaceAtName);
        if(f.replaceAtName.prevStart < v.length){
          accum2.push((f.replaceAtName.prevStart?' ':'')+v.substring(f.replaceAtName.prevStart));
        }
      }else{
        accum2.push(v);
      }
      //console.debug("accum2 =",accum2);
    });
    delete f.accum;
    //console.debug("accum2 =",accum2);
    const span = tgtElem || f.ce('span');
    accum2.forEach(function(e){
      if('string'===typeof e) e = f.ct(e);
      span.appendChild(e);
    });
    //console.debug("span =",span.innerHTML);
    return span;
  }/*end messageToDOM()*/;
  function newcontent(jx){
    var tab = document.getElementById("dialog");
    var i;
    for(i=0; i<jx.msgs.length; ++i){
      let m = jx.msgs[i];
      let r = document.createElement("tr");
      if( m.msgid>mxMsg ) mxMsg = m.msgid;
      tab.insertBefore(r, tab.childNodes[0]);
      let td = document.createElement("td");
      r.appendChild(td);
      if( m.xfrom!=_me ){
        td.appendChild(document.createTextNode(m.xfrom));
      }
      td = document.createElement("td");
      r.appendChild(td);
      let span = document.createElement("span");
      td.appendChild(span);
      if( m.fsize>0 ){
        if( m.fmime && m.fmime.startsWith("image/") ){
          let img = document.createElement("img");
          img.src = "%string($downloadurl)/" + m.msgid;
          span.appendChild(img);
        }else{
          let a = document.createElement("a");
          let txt = "(" + m.fname + " " + m.fsize + " bytes)";
          a.href = "%string($downloadurl)/" + m.msgid;
          a.appendChild(document.createTextNode(txt));
          span.appendChild(a);
        }
        let br = document.createElement("br");
        br.style.clear = "both";
        span.appendChild(br);
      }
      messageToDOM(m.xmsg || "??empty??", span);
      span.classList.add('chat-message');
      if( m.xfrom!=_me ){
        span.classList.add('chat-mx');
      }else{
        span.classList.add('chat-ms');
      }
      td = document.createElement("td");
      r.appendChild(td);
      if( m.xfrom==_me ){
        td.appendChild(document.createTextNode('me'))
      }
    }
    setTimeout(poll, 10);
  }
  async function poll(){
    fetch("%string($pollurl)/" + mxMsg)
    .then(x => x.json()).then(y => newcontent(y));
  }
  poll();
})();</script>
  }

  # Make sure the chat database exists
  sqlite3 db [chat-db-name]
  if {[db one {PRAGMA journal_mode}]!="wal"} {
    db eval {PRAGMA journal_mode=WAL}
  }
  db eval {
    CREATE TABLE IF NOT EXISTS chat(
      msgid INTEGER PRIMARY KEY AUTOINCREMENT,
      mtime JULIANDAY,
      xfrom TEXT,
      xto   TEXT,
      xmsg  TEXT,
      file  BLOB,
      fname TEXT,
      fmime TEXT
    );
    CREATE TABLE IF NOT EXISTS ustat(
      uname TEXT PRIMARY KEY,
      mtime JULIANDAY,  -- Last interaction
      seen  INT,        -- Last message seen
      logout JULIANDAY
    ) WITHOUT ROWID;
  }
  db close
}

# Show the CGI environment.  Used for testing only.
#
proc wapp-page-env {} {
  wapp-trim {
    <div class="fossil-doc" data-title="Chat CGI Environment">
    <pre>%html([wapp-debug-env])</pre>
    </div>
  }
}

# Log the CGI environment into the "-logfile.txt" file in the same
# directory as the script.  Used for testing and development only.
#
proc logenv {} {
  set fn [file dir [wapp-param SCRIPT_FILENAME]]/-logfile.txt
  set out [open $fn a]
  puts $out {************************************************************}
  puts $out [wapp-debug-env]
  close $out
}

# A no-op page.  Used for testing and development only.
#
proc noop-page {} {
  wapp-trim {
    <div class="fossil-doc" data-title="No-op"><h1>No-Op</h1></div>
  }
}

# Accept a new post via XHR.
# No reply expected.
#
proc wapp-page-send {} {
  if {[not-authorized]} return
  set user [wapp-param FOSSIL_USER]
  set fcontent [wapp-param file.content]
  set fname [wapp-param file.filename]
  set fmime [wapp-param file.mimetype]
  set msg [wapp-param msg]
  sqlite3 db [chat-db-name]
  db eval BEGIN
  if {$fcontent!=""} {
    db eval {
      INSERT INTO chat(mtime,xfrom,xmsg,file,fname,fmime)
      VALUES(julianday('now'),$user,@msg,@fcontent,$fname,$fmime)
    }
  } else {
    db eval {
      INSERT INTO chat(mtime,xfrom,xmsg)
      VALUES(julianday('now'),$user,@msg)
    }
  }
  db eval {
    INSERT INTO ustat(uname,mtime,seen) VALUES($user,julianday('now'),0)
    ON CONFLICT(uname) DO UPDATE set mtime=julianday('now')
  }
  db eval COMMIT
  db close
}

# Request updates.
# Delay the response until something changes (as this system works
# using the Hanging-GET or Long-Poll style of server-push).
# The result is javascript describing the new content.
#
# Call is like this:   /poll/N
# Where N is the last message received so far.  The reply stalls
# until newer messages are available.
#
proc wapp-page-poll {} {
  if {[not-authorized]} return
  wapp-mimetype text/json
  set msglist {}
  sqlite3 db [chat-db-name]
  set id 0
  scan [wapp-param PATH_TAIL] %d id
  while {1} {
    set datavers [db one {PRAGMA data_version}]
    db eval {SELECT msgid, datetime(mtime) AS dx, xfrom, CAST(xmsg AS text) mx,
                    length(file) AS lx, fname, fmime
               FROM chat
              WHERE msgid>$id
              ORDER BY msgid} {
      set quname [string map {\" \\\"} $xfrom]
      set qmsg [string map {\" \\\"} $mx]
      if {$lx==""} {set lx 0}
      set qfname [string map {\" \\\"} $fname]
      lappend msglist "\173\"msgid\":$msgid,\"mtime\":\"$dx\",\
        \"xfrom\":\"$quname\",\
        \"xmsg\":\"$qmsg\",\"fsize\":$lx,\
        \"fname\":\"$qfname\",\"fmime\":\"$fmime\"\175"
    }
    if {[llength $msglist]>0} {
      wapp-unsafe "\173\042msgs\042:\133[join $msglist ,]\135\175"
      db close
      return
    }
    after 2000
    while {[db one {PRAGMA data_version}]==$datavers} {after 2000}
  }
}

# Show the text of this script.
#
proc wapp-page-self {} {
  wapp-trim {
    <div class="fossil-doc" data-title="Wapp Script for Chat">
  }
  set fd [open [wapp-param SCRIPT_FILENAME] rb]
  set script [read $fd]
  wapp-trim {
    <pre>%html($script)</pre>
  }
  wapp-trim {
    </div>
  }
}

# Download the file associated with a message.
#
# Call like this:   /download/N
# Where N is the message id.
#
proc wapp-page-download {} {
  if {[not-authorized]} {
    wapp-trim {
      <h1>Not authorized</h1>
      <p>You must have privileges to use this chatroom</p>
      </div>
    }
    return
  }
  set id 0
  scan [wapp-param PATH_TAIL] %d id
  sqlite3 db [chat-db-name]
  db eval {SELECT fname, fmime, file FROM chat WHERE msgid=$id} {
    wapp-mimetype $fmime
    wapp $file
  }
  db close
}


wapp-start $argv
