# Fossil Chat

## Introduction

As of version 2.14 (and prerelease versions after about 2020-12-25),
Fossil supports a developer chatroom feature.  The chatroom provides an
ephemeral discussion venue for insiders.  Design goals include:

  *  **Simple but functional** &rarr; Fossil chat is designed to provide a
     convenient real-time communication mechanism for geographically
     dispersed developers.  Fossil chat is emphatically *not* intended
     as a replacement or 
     competitor for IRC, Slack, Discord, Telegram, Google Hangouts, etc.

  *  **Low administration** &rarr;
     There is no additional set up or configuration.
     Simply enable the [C capability](/setup_ucap_list) for users
     whom you want to give access to the chatroom.

  *  **Ephemeral** &rarr;
     Chat messages do not sync to peer repositories.  And they are
     automatically deleted after a configurable delay (default: 7 days).
     They can be deleted at any time without impacting any other part
     of the system.

Fossil chat is designed for use by insiders - people with check-in
privileges or higher.  It is not intended as a general-purpose gathering
place for random passers-by on the internet. 
Fossil chat seeks to provide a communication venue for discussion
that does *not* become part of the permanent record for the project.
For persist and durable discussion, use the [Forum](./forum.wiki).
Because the conversation is intended to be ephemeral, the chat messages
are local to a single repository.  Chat content does not sync.


## Setup

A Fossil repository must be functioning as a [server](./server/) in order
for chat to work.
To activate chat, simply add the [C capability](/setup_ucap_list)
to every user who is authorized to participate.  Anyone who can read chat
can also post to chat.

Setup ("s") and Admin ("a") users always have access to chat, without needing
the "C" capability.  A common configuration is to add the "C" capability
to "Developer" so that any individual user who has the "v" capability will
also have access to chat.

There are also some settings under /Admin/Chat that control the
behavior of chat, though the default settings are reasonable so in most
cases those settings can be ignored.  The settings control things like
the amount of time that chat messages are retained before being purged
from the repository database.

## Usage

For users with appropriate permissions simply browse to the
[/chat](/help?cmd=/chat) to start up a chat session.  The default
skin includes a "Chat" entry on the menu bar on wide screens for
people with chat privilege.  There is also a "Chat" option on
the [Sitemap page](/sitemap), which means that chat will appears
as an option under the hamburger menu for many [skins](./customskin.md).

Message text is delivered verbatim.  There is no markup.  However,
the chat system does try to identify and tag hyperlinks, as follows:

  *  Any word that begins with "http://" or "https://" is assumed
     to be a hyperlink and is tagged.

  *  Text within `[...]` is parsed and it if is a valid hyperlink
     target (according to the way that [Fossil Wiki](/wiki_rules) or
     [Markdown](/md_rules) understand hyperlinks) then that text
     is tagged.

Apart from adding hyperlink anchor tags to bits of text that look
like hyperlinks, no changes are made to the input text.

## Aural Alerts

If you have a local clone and checkout for a remote Fossil repository
and that remote repository supports chat,
then you can bring up a chat window for that remote repository
that will beep whenever new content arrives.  This must be done from a
terminal window.
Change directories to a working checkout of the local clone and type:

>    fossil chat

This command will bring up a chat window in your default web-browser
(similar to the way the "[fossil ui](/help?cmd=ui)" does).   The
chat will be for the remote repository, the repository whose URL shows
when you type the "[fossil remote](/help?cmd=remote)" command.  In
addition to bringing up the chat window, this command will also
send a single "bel" character (U+0007) to standard error of the terminal
whenever new messages arrive in the chat window.  On most systems,
the terminal windows will emit an "beep" whenever they receive the U+0007
character.  This works out-of-the-box for Mac and Windows, but on some
flavors of Linux, you might need to enable terminal beeps in your system
preferences.

In theory, it should be possible to get a web-browser to make an alert
sound whenever new content arrives in the chat window.  However, the
Fossil developers have been unable to figure out how to do that.
Web-browsers make it very difficult to play sounds that are
not the direct result of a user-click, probably to prevent
advertisements from pestering users with a cacophony of alerts.


## Implementation Details

*You do not need to understand how Fossil chat works in order to use it.
But many developers prefer to know how their tools work.
This section is provided for the benefit of those curious developers.*

The [/chat](/help?cmd=/chat) webpage downloads a small amount of HTML
and a small amount of javascript to run the chat session.  The
javascript uses XMLHttpRequest (XHR) to download chat content, post
new content, or delete historical messages.  The following web
interfaces are used by the XHR:

  *  **/chat-poll** &rarr;
     Download chat content as JSON.
     Chat messages are number sequentially.
     The client tells the server the largest chat message it currently
     holds and the server sends back subsequent messages.  If there
     are no subsequent messages, the /chat-poll page blocks until new
     messages are available.

  *  **/chat-send** &rarr;
     Sends a new chat message to the server.

  *  **/chat-delete** &rarr;
     Delete a chat message.

  *  **/chat-ping** &rarr;
     An HTTP request to this page on the loopback IP address causes
     a single U+0007 "bel" character to be sent to standard error of
     the controlling terminal.  This is used to implement
     aural alerts with the "[fossil chat](/help?cmd=chat)" command.

Fossil chat uses the venerable "hanging GET" or 
"[long polling](wikipedia:/wiki/Push_technology#Long_polling)"
technique to recieve asynchronous notification of new messages.
This is done because long polling works well with CGI and SCGI,
which are the usual mechanisms for setting up a Fossil server.
More advanced notification techniques such as 
[Server-sent events](wikipedia:/wiki/Server-sent_events) and especially
[WebSockets](wikipedia:/wiki/WebSocket) might seem more appropriate for
a chat system, but those technologies are not compatible with CGI.

Downloading of posted files and images uses a separate, non-XHR interface:

  * **/chat-download** &rarr;
    Fetches the file content associated with a post (one file per
    post, maximum). In the UI, this is accessed via links to uploaded
    files and via inlined image tags.

Chat messages are stored on the server-side in the CHAT table of
the repository.

~~~
   CREATE TABLE repository.chat(
      msgid INTEGER PRIMARY KEY AUTOINCREMENT,
      mtime JULIANDAY,
      ltime TEXT,
      xfrom TEXT,
      xmsg  TEXT,
      fname TEXT,
      fmime TEXT,
      mdel  INT,
      file  BLOB)
    );
~~~

The CHAT table is not cross-linked with any other tables in the repository
schema.  An administrator can "DROP TABLE chat;" at any time, without
harm (apart from deleting all chat history, of course).  The CHAT table
is dropped when running [fossil scrub --verily](/help?cmd=scrub).

On the server-side, message text is stored exactly as entered by the
users.  The /chat-poll page queries the CHAT table and constructs a
JSON reply described in the [/chat-poll
documentation](/help?cmd=/chat-poll).  The message text is translated
into HTML prior to being converted into JSON so that the text can be
safely added to the display using assignment to `innerHTML`. Though
`innerHTML` assignment is generally considered unsafe, it is only so
with untrusted content from untrusted sources. The chat content goes
through sanitation steps which eliminate any potential security
vulnerabilities of assigning that content to `innerHTML`.
