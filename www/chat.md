# Fossil Chat

## Introduction

Fossil’s developer chatroom feature provides an
ephemeral discussion venue for insiders.  Design goals include:

  *  **Simple but functional** &rarr;
     Fossil chat is designed to provide a convenient real-time
     communication mechanism for geographically dispersed developers.
     Fossil chat is *not* intended as a replacement or competitor for
     IRC, Slack, Discord, Telegram, Google Hangouts, etc.

  *  **Low administration** &rarr;
     You can activate the chatroom in seconds without having to
     mess with configuration files or install new software.
     In an existing [server setup](./server/),
     simply enable the [C capability](/setup_ucap_list) for users
     whom you want to give access to the chatroom.

  *  **Ephemeral** &rarr;
     Chat messages do not sync to peer repositories, and they are
     automatically deleted after a configurable delay (default: 7 days).
     Individual messages or the entire conversation
     can be deleted at any time without impacting any other part
     of the system.

Fossil chat is designed for use by insiders - people with check-in
privileges or higher.  It is not intended as a general-purpose gathering
place for random passers-by on the internet. 
Fossil chat seeks to provide a communication venue for discussion
that does *not* become part of the permanent record for the project.
For persistent and durable discussion, use the [Forum](./forum.wiki).
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

## <a id="usage"></a>Usage

For users with appropriate permissions, simply browse to the
[/chat](/help?cmd=/chat) to start up a chat session.  The default
skin includes a "Chat" entry on the menu bar on wide screens for
people with chat privilege.  There is also a "Chat" option on
the [Sitemap page](/sitemap), which means that chat will appear
as an option under the hamburger menu for many [skins](./customskin.md).

Chat messages are subject to [Fossil's
full range of Markdown processing](/md_rules). Because chat messages are
stored as-is when they arrive from a client, this change applies
retroactively to messages stored by previous fossil versions.

Files may be sent via chat using the file selection element at the
bottom of the page. If the desktop environment system supports it,
files may be dragged and dropped onto that element. Files are not
automatically sent - selection of a file can be cancelled using the
Cancel button which appears only when a file is selected. When the
Send button is pressed, any pending text is submitted along with the
selected file. Image files sent this way will, by default, appear
inline in messages, but each user may toggle that via the settings
popup menu, such that images instead appear as downloadable links.
Non-image files always appear in messages as download links.

### Deletion of Messages

<div class="sidebar">Message deletion is itself a type of message, which
is why deletions count towards updates in the recent activity list.  (It
is counted for the person who performed the deletion, not the author of
the deleted comment.) That can potentially lead to odd corner cases
where a user shows up in the list but has no messages which are
currently visible because they were deleted, or an admin user who has
not posted anything but deleted a message. That is a known minor
cosmetic-only bug with a resolution of "will not fix."</div>

Any user may *locally* delete a given message by clicking on the "tab"
at the top of the message and clicking the button which appears. Such
deletions are local-only, and the messages will reappear if the page
is reloaded. The user who posted a given message, or any Admin users,
may additionally choose to globally delete a message from the chat
record, which deletes it not only from their own browser but also
propagates the removal to all connected clients the next time they
poll for new messages.

### <a id='notifications'></a>Customizing New-message Notification Sounds

By default, the list of new-message notification sounds is limited to
a few built in to the fossil binary. In addition, any
[unversioned files](./unvers.wiki) named `alert-sounds/*.{mp3,wav,ogg}`
will be included in that list. To switch sounds, tap the "settings"
button.

### <a id='connection'></a> Who's Online?

Because the chat app has to be able to work over transient CGI-based
connections, as opposed to a stable socket connection to the server,
real-time tracking of "who's online" is not feasible.
Chat offers an optional feature, toggleable in the settings,
which can list users who have posted messages in the client's current
list of loaded messages. This is not the same thing as tracking who's
online, but it gives an overview of which users have been active most
recently, noting that "lurkers" (people who post no messages) will not
show up in that list, nor does the chat infrastructure have a way to
track and present those. That list can be used to filter messages on a
specific user by tapping on that user's name, tapping a second time to
remove the filter.

### <a id="cli"></a> The `fossil chat` Command

Type [fossil chat](/help?cmd=chat) from within any open check-out
to bring up a chatroom for the project that is in that checkout.
The new chat window will attempt to connect to the default sync
target for that check-out (the server whose URL is shown by the
[fossil remote](/help?cmd=remote) command).

### <a id="robots"></a> Chat Messages From Robots

The [fossil chat send](/help?cmd=chat) can be used by project-specific
robots to send notifications to the chatroom.  For example, on the
[SQLite project](https://sqlite.org/) (for which the Fossil chatroom
feature, and indeed all of Fossil, was invented) there are long-running
fuzz servers that sometimes run across obscure problems.  Whenever this
happens, a message is sent to the SQLite developers chatroom alerting
them to the problem.

The recommended way to allow robots to send chat messages is to create
a new user on the server for each robot.  Give each such robot account
the "C" privilege only.  That means that the robot user account will be 
able to send chat messages, but not do anything else.  Then, in the
program or script that runs the robot, when it wants to send a chat
message, have it run a command like this:

~~~~
fossil chat send --remote https://robot:PASSWORD@project.org/fossil \
  --message 'MESSAGE TEXT' --file file-to-attach.txt
~~~~

Substitute the appropriate project URL, robot account
name and password, message text and file attachment, of course.

### <a id="chat-robot"></a> Chat Messages For Timeline Events

If the [chat-timeline-user setting](/help?cmd=chat-timeline-user) is not an
empty string, then any change to the repository that would normally result
in a new timeline entry is announced in the chatroom.  The announcement
appears to come from a user whose name is given by the chat-timeline-user
setting.

This mechanism is similar to [email notification](./alerts.md) except that
the notification is sent via chat instead of via email.

## Quirks

  - There is no message-editing capability. This is by design and
    desire of `/chat`'s developers.

  - When `/chat` has problems connecting to the message poller (see
    the next section) it will provide a subtle visual indicator of the
    connection problem - a dotted line along the top of the input
    field.  If the connection recovers within a short time, that
    indicator will go away, otherwise it will pop up a loud message
    signifying that the connection to the poller is down and how long
    it will wait to retry (progressively longer, up to some
    maximum).  `/chat` will recover automatically when the server is
    reachable.  Trying to send messages while the poller connection is
    down is permitted, and the poller will attempt to recover
    immediately if sending of a message succeeds. That applies to any
    operations which send traffic, e.g. if the per-message "toggle
    text mode" button is activated or a message is globally deleted.

## Implementation Details

*You do not need to understand how Fossil chat works in order to use it.
But many developers prefer to know how their tools work.
This section is provided for the benefit of those curious developers.*

The [/chat](/help?cmd=/chat) webpage downloads a small amount of HTML
and a small amount of javascript to run the chat session.  The
javascript uses XMLHttpRequest (XHR) to download chat content, post
new content, or delete historical messages.  The following web
interfaces are used by the XHR:

  *  [/chat-poll](/help?name=/chat-poll) &rarr;
     Downloads chat content as JSON.
     Chat messages are numbered sequentially.
     The client tells the server the largest chat message it currently
     holds, and the server sends back subsequent messages.  If there
     are no subsequent messages, the /chat-poll page blocks until new
     messages are available.

  *  [/chat-send](/help?name=/chat-send) &rarr;
     Sends a new chat message to the server.

  *  [/chat-delete](/help?name=/chat-delete) &rarr;
     Deletes a chat message.

Fossil chat uses the venerable "hanging GET" or 
"[long polling](wikipedia:/wiki/Push_technology#Long_polling)"
technique to receive asynchronous notification of new messages.
This is done because long polling works well with CGI and SCGI,
which are the usual mechanisms for setting up a Fossil server.
More advanced notification techniques such as 
[Server-sent events](wikipedia:/wiki/Server-sent_events) and especially
[WebSockets](wikipedia:/wiki/WebSocket) might seem more appropriate for
a chat system, but those technologies are not compatible with CGI.

Downloading of posted files and images uses a separate, non-XHR interface:

  * [/chat-download](/help?name=/chat-download) &rarr;
    Fetches the file content associated with a post (one file per
    post, maximum). In the UI, this is accessed via links to uploaded
    files and via inlined image tags.

Chat messages are stored on the server-side in the CHAT table of
the repository.

~~~
CREATE TABLE repository.chat(
  msgid INTEGER PRIMARY KEY AUTOINCREMENT,
  mtime JULIANDAY,  -- Time for this entry - Julianday Zulu
  lmtime TEXT,      -- Client YYYY-MM-DDZHH:MM:SS when message originally sent
  xfrom TEXT,       -- Login of the sender
  xmsg  TEXT,       -- Raw, unformatted text of the message
  fname TEXT,       -- Filename of the uploaded file, or NULL
  fmime TEXT,       -- MIMEType of the upload file, or NULL
  mdel INT,         -- msgid of another message to delete
  file  BLOB        -- Text of the uploaded file, or NULL
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
into HTML before being converted to JSON so that the text can be
safely added to the display using assignment to `innerHTML`. Though
`innerHTML` assignment is generally considered unsafe, it is only so
with untrusted content from untrusted sources. The chat content goes
through sanitization steps which eliminate any potential security
vulnerabilities of assigning that content to `innerHTML`.
