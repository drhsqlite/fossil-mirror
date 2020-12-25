# Fossil Chat

## Introduction

As of version 2.14 (and prerelease versions after about 2020-12-25),
Fossil supports a developer chatroom feature.  The chatroom provides an
ephemeral discussion forum for insiders.  Design goals include:

  *  **Simple but functional** &rarr; Fossil chat is designed to provide a
     convenient real-time communication mechanism for geographically
     distributed developers.  It is not intended as a replace or 
     competitor for IRC, Slack, Discord, Telegram, Google Hangouts, etc.

  *  **Low administrative overhead** &rarr;
     There is nothing new to set up or configure.
     Simply enable the [C capability](/setup_ucap_list) for users
     whom you want to give access to the chatroom.

  *  **Ephemeral** &rarr;
     Chat messages do not sync to peer repositories.  And they are
     automatically deleted after a configurable delay (default: 7 days).

Fossil chat is designed to provide a communication venue for discussion
that does *not* become part of the permanent record for the project.
For persist and syncable discussion, use the [Forum](./forum.wiki).

Fossil chat is designed for use by insiders - people with check-in
privileges or higher.  It is not intended as a general-urpose gathering
place for random passers-by on the internet.  (It could be used for that,
in theory, but its feature set is not designed with that use case in mind.)

Fossil chat is designed for transient, ephemerial, real-time discussion.
The conversation is local to a single repository and is not synced or
retained long-term.

Fossil chat is specific to a single repository.  It is only really useful
if you configure a [common server repository](./server/) that all chat
participants can connect to.

## Setup

To activate Fossil chat, simply add the [C capability](/setup_ucap_list)
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
the [Sitemap page](/sitemap) (which is linked to the hamburger menu
of many skins).

Message text is delivered verbatim.  There is no markup.  However,
the chat system does try to identify and tag hyperlinks, as follows:

  *  Any word that begins with "http://" or "https://" is assumed
     to be a hyperlink and is tagged.

  *  Text within `[...]` is parsed and it if is a valid hyperlink
     target (according to the way that [Fossil Wik](/wiki_rules) or
     [Markdown](/md_rules) understand hyperlinks) then that text
     is tagged.

Apart from adding hyperlink anchor tags to bits of text that look
like hyperlinks, no changes are made to the input text.

## Implementation Details

*(This section is informational only.  You do not need to understand
how Fossil chat works in order to use it.  But many developers prefer
to know what is happening "under the hood".)*

The [/chat](/help?cmd=/chat) webpage downloads a small amount of
HTML and a few KB of javascript to run the chat session.  The 
javascript uses XMLHttpRequest (XHR) to download chat content,
post not content, or delete historical message.  The following
web interfaces are used by the XHR:

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

The Fossil chat design uses the traditional "hanging GET" or 
"[long polling](wikipedia:/wiki/Push_technology#Long_polling)"
to wait for new chat messages.  This is done because that technique works
easily with CGI and SCGI, which are the usual mechanisms for setting up
a Fossil server.  More advanced techniques such as 
[Server-sent events](wikipedia:/wiki/Server-sent_events) and especially
[WebSockets](wikipedia:/wiki/WebSocket) might seem more appropriate for
a chat system, but those technologies are not readily compatible with CGI.

Chat messages are stored on the server-side in the CHAT table of
the repository.

~~~
   CREATE TABLE repository.chat(
      msgid INTEGER PRIMARY KEY AUTOINCREMENT,
      mtime JULIANDAY,
      xfrom TEXT,
      xmsg  TEXT,
      file  BLOB,
      fname TEXT,
      fmime TEXT,
      mdel INT)
    );
~~~

The CHAT table is not cross-linked with any other tables in the repository
schema.  An administrator can "DELETE TABLE chat;" at any time, without
harm (apart from deleting all chat history, of course).  The CHAT table
is dropped when running [fossil scrub --verily](/help?cmd=scrub).

On the server-side, message text is stored exactly as entered by the
users.  The /chat-poll page queries the CHAT table and constructs
a JSON reply described in the [/chat-poll documentation](/help?cmd=/chat-poll).
The message text is translated into HTML prior to being converted into
JSON so that the text can be safely added to the display using
innerHTML.
