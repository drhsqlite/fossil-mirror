# Email Alerts

## Overview

Beginning with version 2.7, Fossil can send email messages to
subscribers to alert them to changes in the repository:

  *  New [checkins](/help?cmd=ci)
  *  [Ticket](./tickets.wiki) changes
  *  [Wiki](./wikitheory.wiki) page changes
  *  New and edited [forum](./forum.wiki) posts
  *  Users receiving [new permissions](./caps/index.md) (admins only)
  *  Announcements

Subscribers can elect to receive emails as soon as these events happen,
or they can receive a daily digest of the events instead.

Email alerts are sent by a [Fossil server](./server/), which must be
[set up](#quick) by the Fossil administrator to send email.

Email alerts do not currently work if you are only using Fossil from the
command line.

A bit of terminology: Fossil uses the terms "email alerts" and
"notifications" interchangeably. We stick to the former term in this
document except when referring to parts of the Fossil UI still using the
latter term.


## Setup Prerequisites

Much of this document describes how to set up Fossil's email alert
system. To follow this guide, you will need a Fossil UI browser window
open to the [Admin → Notification](/setup_notification) Fossil UI screen
on the Fossil server that will be sending these email alerts, logged
in as a user with [**Admin** capability](./caps/ref.html#a). It is not possible to work on a
clone of the server's repository and push the configuration changes up
to that repo as an Admin user, [on purpose](#backup).

<a id="cd"></a>
You will also need a CLI window open with its working directory changed
to a checkout directory of the Fossil repository you are setting up to
send email.  If you don't `cd` to such a checkout directory first,
you'll need to add `-R /path/to/repo.fossil` to each `fossil` command
below to tell Fossil which repository you mean it to apply the command
to.

There are other prerequisites for email service, but since they vary
depending on the configuration you choose, we'll cover these inline
below.


<a id="quick"></a>
## Quick Email Service Setup

If you've already got a working OpenSMTPD, Postfix, Exim, Sendmail,
or similar server on the machine running your Fossil instance(s),
and you aren't using Fossil's [chroot jail feature](./chroot.md)
to wall Fossil off from the rest of the machine, it's
fairly simple to set up email alerts.

(Otherwise, skip [ahead](#advanced) to the sections on advanced email
service setup.)

This is our "quick setup" option even though setting up an SMTP mail
server is not trivial, because there are many other reasons to have such
a server set up already: internal project email service, `cron`
notifications, server status monitoring notifications...

With that out of the way, the Fossil-specific steps are easy:

1.  Go to [Admin → Notification](/setup_notification) and fill out all
    of the **Required** fields:

    * **Canonical server URL** — Use the suggested URL

    * **"From" email address** — `forum-bounces@example.com` is
      traditional, but suit yourself

    * **Repository nickname** — See the suggested examples on the web page.

2.  Set "Email Send Method" to "Pipe to a command"

3.  Set the "Administrator email address" to a suitable valid email
    address on that machine.  It could be the same value you used for
    the "From" address above, or it could be a different value like
    `admin@example.com`.

Save your changes.

At the command line, say

    $ fossil set email-send-command

If that gives a blank value instead of `sendmail -ti`, say

    $ fossil set email-send-command "sendmail -ti"

to force the setting. That works around a [known
bug](https://fossil-scm.org/forum/forumpost/840b676410) which may be
squished by the time you read this.

If you're running Postfix or Exim, you might think that command is
wrong, since you aren't running Sendmail. These mail servers provide a
`sendmail` command for compatibility with software like Fossil that has
no good reason to care exactly which SMTP server implementation is
running at a given site. There may be other SMTP servers that also
provide a compatible `sendmail` command, in which case they may work
with Fossil using the same steps as above.

<a id="status"></a>
If you reload the Admin → Notification page, the Status section at the
top should show:

    Outgoing Email: Piped to command "sendmail -ti"
    Pending Alerts: 0 normal, 0 digest
    Subscribers:    0 active, 0 total

Before you move on to the next section, you might like to read up on
[some subtleties](#pipe) with the "pipe to a command" method that we did
not cover above.


<a id="usage"></a>
## Usage and Testing

Now that email service from Fossil is set up, you can test it and begin
using it.


<a id="sub" name="subscribe"></a>
### Subscribing to Alerts

In the Status output above, we saw that there are no subscribers, so the
next step is to add the first one.

Go to the `/subscribe` page on your Fossil instance to sign up for email
alerts.  At the very least, you will need to sign up for "Forum Posts"
and "Announcements" to complete the testing steps below.

If you're logged in with a Fossil repository user account and put the
same user name and email address into this forum as you used for your
user information under Admin → Users, Fossil will simply tie your alert
preferences to your login record, and the email address in your user's
Contact Info field will be considered already-verified.  Otherwise,
Fossil will create an alert-only record, and you will have to verify the
email address before Fossil will send alerts to it.

This shows a key aspect of the way Fossil's email alerts system works,
by the way: a user can be signed up for email alerts without having a
full-fledged Fossil user account. Only when both user names are the same
are the two records tied together under the hood. For more on this, see
[Users vs Subscribers below](#uvs).

If you are seeing the following complaint from Fossil:

> Use a different login with greater privilege than FOO to access /subscribe

...then the repository's administrator forgot to give the
[**EmailAlert** capability][cap7]
to that user or to a user category that the user is a member of.

After a subscriber signs up for alerts for the first time, a single
verification email is sent to that subscriber's given email address.
The new subscriber must click a link in that email in order to activate
the subscription.

Subscription verification emails are only sent once.  This is a defense
against malicious robots that try to harass innocent Internet users by
having subscription pages send multiple verification emails.  If the
initial subscription verification does not go through correctly, an
administrator must [intervene](#admin) to reset the subscription.

Every subscriber-only email address has a [long random hexadecimal
security code](#scode) that serves in place of a password.  All email
alerts contain a link in their footer back to the Fossil server,
incorporating this security code, which allows the subscriber to adjust
their subscription options.  If a user doesn't have any of those emails,
they can request a link via email by visiting the `/alerts` or
`/unsubscribe` page on the repository.

Those with Fossil repository logins can adjust their email alert
settings by visiting the `/alerts` page on the repository.  With the
default skin, you can get there by clicking the "Logout" link in the
upper right corner of any Fossil UI page then clicking the "Email
Alerts" link.  That link is also available via the Sitemap (`/sitemap`)
and via the default skin's hamburger menu (&#9776;).

[cap7]: ./caps/ref.html#7


<a id="unsub" name="unsubscribe"></a>
### Unsubscribing

To unsubscribe from alerts, visit the `/alerts` page on the repository,
click the "Unsubscribe" button, then check the "Unsubscribe" checkbox to
verify your action and press the "Unsubscribe" button a second time.

This interlock is intended to prevent accidental unsubscription.


<a id="test"></a>
### Test Email Service

The easiest way to test email sending from Fossil is via the "[Send
Announcement](/announce)" link at the top of the "Email Notification
Setup" page.  Put your email address in the "To:" line and a test
message below, then press "Send Message" to verify that outgoing email
is working.

Another method is from the command line:

    $ fossil alerts test-message you@example.com --body README.md --subject Test

That should send you an email with "Test" in the subject line and the
contents of your project's `README.md` file in the body.

That command assumes that your project contains a "readme" file, but of
course it does, because you have followed the [Programming Style Guide
Checklist][cl], right? Right.

[cl]: https://sendgrid.com/blog/programming-style-guide-checklist/


<a id="cap7" name="ucap"></a>
### User Capabilities

Once email alerts are working, you may need to [adjust the default user
capabilities](./caps/) to give "[Email Alerts][cap7]" capability to any
[user category](./caps/#ucat) or [individual user](./caps/#ucap) that
needs to use the subscription setup pages, `/subscribe` and `/alerts`.
[**Admin**][capa] and [**Setup**][caps] users always have this
capability.

To allow any passer-by on the Internet to subscribe, give the "Email
Alerts" capability to the "nobody" user category.  To require that a
person solve a simple CAPTCHA first, give that capability to the
"anonymous" user category instead.

[capa]: ./caps/ref.html#a
[caps]: ./caps/ref.html#s


<a id="first" name="frist"></a>
### First Post

I suggest taking the time to compose a suitable introductory message
especially for your project's forum, one which a new user would find
helpful.

Wait a few seconds, and you should receive an email alert with the
post's subject and body text in the email.


<a id="trouble"></a>
### Troubleshooting

If email alerts aren't working, there are several useful commands you
can give to figure out why.

(Be sure to [`cd` into a repo checkout directory](#cd) first!)

    $ fossil alerts status

This should give much the same information as you saw [above](#status).
One difference is that, since you've created a forum post, the
`pending-alerts` value should only be zero if you did in fact get the
requested email alert. If it's zero, check your mailer's spam folder. If
it's nonzero, continue with these troubleshooting steps.

    $ fossil backoffice

That forces Fossil to run its ["back office" process](./backoffice.md).
Its only purpose at the time of this writing is to push out alert
emails, but it might do other things later. Sometimes it can get stuck
and needs to be kicked. For that reason, you might want to set up a
crontab entry to make sure it runs occasionally.

    $ fossil alerts send

This should also kick off the backoffice processing, if there are any
pending alerts to send out.

    $ fossil alert pending

Show any pending alerts. The number of lines output here should equal
the [status output above](#status).

    $ fossil test-add-alerts f5900
    $ fossil alert send

Manually create an email alert and push it out immediately.

The `f` in the first command's final parameter means you're scheduling a
"forum" alert. The integer is the ID of a forum post, which you can find
by visiting `/timeline?showid` on your Fossil instance.

The second command above is necessary because the `test-add-alerts`
command doesn't kick off a backoffice run.

    $ fossil ale send

This only does the same thing as the final command above, rather than
send you an ale, as you might be hoping. Sorry.


<a id="advanced"></a>
## Advanced Email Setups

Fossil offers several methods of sending email:

  1.  Pipe the email message text into a command.
  2.  Store email messages as entries in an SQLite database.
  3.  Store email messages as individual files in a directory.
  4.  Send emails to an SMTP relay.
  5.  Send emails directly to the recipients via SMTP.

This wide range of options allows Fossil to talk to pretty much any
SMTP setup.

The first four options let Fossil delegate email handling to an existing
[MTA] so that Fossil does not need to implement the [roughly two
dozen][mprotos] separate [RFCs][rfcs] required in order to properly
support SMTP email in this complex world we've built.  As well, this
design choice means you do not need to do duplicate configuration, such
as to point Fossil at your server's TLS certificate in order to support
users behind mail servers that require STARTTLS encryption.

[mprotos]: http://sqlite.1065341.n5.nabble.com/Many-ML-emails-going-to-GMail-s-SPAM-tp98685p98722.html
[rfcs]:    https://en.wikipedia.org/wiki/Request_for_Comments


<a id="pipe"></a>
### Method 1: Pipe to a Command

This is our ["quick setup" option](#quick) above, but there are some
details we ignored which we'll cover now.

Fossil pipes the email message in [RFC 822 format][rfc822] to the
standard input of the command you gave as the "Email Send Method",
defaulting to `sendmail -ti`. This constitutes a protocol between Fossil
and the SMTP [message transfer agent (MTA)][MTA]. Any other MTA which
speaks the same protocol can be used in place of the most common
options: Sendmail, Exim, and Postfix.

The `-t` option tells the command to expect the list of email recipients
in a `To` header in the RFC 822 message presented on its standard input.
Without this option, the `sendmail` command expects to receive the
recipient list on the command line, but that's not possible with the
current design of this email sending method. Therefore, if you're
attempting to use a less common MTA which cannot parse the recipient
list from the `To` header in the email message, you might need to look
for a different MTA.

The `-i` option is only needed for MTAs that take a dot/period at the
beginning of a line of standard input text as "end of message." Fossil
doesn't attempt to escape such dots, so if the line wrapping happens to
occur such that a dot or period in an alert message is at the beginning
of a line, you'll get a truncated email message without this option.
Statistically, this will happen about once every 70 or so messages, so
it is important to give this option if your MTA treats leading dots on a
line this way.

<a id="msmtp"></a>
The [`msmtp`][msmtp] SMTP client is compatible with this
protocol if you give it the `-t` option. It’s a useful option on a server
hosting a Fossil repository which doesn't otherwise require a separate
SMTP server for other purposes, such as because you’ve got a separate
provider for your email and merely need a way to let Fossil feed
messages into it.

It is probably also possible to configure [`procmail`][pmdoc] to work
with this protocol. If you know how to do it, a patch to this document
or a how-to on [the Fossil forum][ff] would be appreciated.

[ff]:     https://fossil-scm.org/forum/
[msmtp]:  https://marlam.de/msmtp/
[MTA]:    https://en.wikipedia.org/wiki/Message_transfer_agent
[pmdoc]:  http://pm-doc.sourceforge.net/doc/
[rfc822]: https://www.w3.org/Protocols/rfc822/


<a id="db"></a>
### Method 2: Store in a Database

The self-hosting Fossil repository at <https://fossil-scm.org/>
currently uses this method rather than [the pipe method](#pipe) because
it is running inside of a restrictive [chroot jail][cj] which is unable
to hand off messages to the local MTA directly.

When you configure a Fossil server this way, it adds outgoing email
messages to an SQLite database file.  A separate daemon process can then
extract those messages for further disposition.

Fossil includes a copy of [the daemon](/file/tools/email-sender.tcl)
used on `fossil-scm.org`: it is just a short Tcl script that
continuously monitors this database for new messages and hands any that
it finds off to a local MTA using the same [pipe to MTA protocol](#pipe)
as above.

In this way, outbound email alerts escape the chroot jail without
requiring that we insert a separate MTA configuration inside that jail.
We only need to arrange that the same SQLite DB file be visible both
inside and outside the chroot jail, which we do by naming the database
file in the "Store Emails In This Database" setting under Admin →
Notification.  The Tcl script has this path hard-coded as
`/home/www/fossil/emailqueue.db`, but you will probably need to adjust
that for your local purposes.

This method may work with other similar technologies besides `chroot`:
Docker containers, LXC containers, BSD jails, Solaris zones, etc.

With suitable file share mappings, this method may even work with
virtual machine or distributed computing setups where the MTA and Fossil
servers are not on the same machine, though beware the [risk of DB
corruption][rdbc] if used with a file sharing technology that doesn't
use proper file locking.

You can start this Tcl script as a daemon automatically on most Unix and
Unix-like systems by adding the following line to the `/etc/rc.local`
file of the server that hosts the repository sending email alerts:

    /usr/bin/tclsh /home/www/fossil/email-sender.tcl &

[cj]:   https://en.wikipedia.org/wiki/Chroot
[rdbc]: https://www.sqlite.org/howtocorrupt.html#_filesystems_with_broken_or_missing_lock_implementations


<a id="dir"></a>
### Method 3: Store in a Directory

This method is functionally very similar to [the DB method](#db),
differing only in that messages are written to a directory in the
filesystem.  You should therefore read that section and make the minor
adjustments required by the storage method.

This method may work over a file sharing mechanism that doesn't do file
locking properly, as long as the reading process is somehow restricted
from reading a message file as it's being written.

It might be useful in testing and debugging to temporarily switch to
this method, since you can easily read the generated email messages
without needing to involve an [MTA].


<a id="relay"></a>
### Method 4: SMTP Relay

In this configuration, the Fossil server contacts an open SMTP relay and
sends the messages to it. This method is only appropriate when:

1. You have a local MTA that doesn't accept [the pipe
   protocol](#pipe).

2. The MTA is willing to accept anonymous submissions, since Fossil
   currently has no way to authenticate itself to the MTA.  This is [an
   unsafe configuration][omr] in most cases, but some SMTP servers make
   an exception for connections coming from a `localhost` or LAN
   address, choosing to accept such submissions as inherently safe.

If you have a local MTA meeting criterion #1 but not #2, we'd suggest
using a more powerful SMTP client such as [msmtp](#msmtp) along with one
of the other methods above.

[omr]: https://en.wikipedia.org/wiki/Open_mail_relay


<a id="direct"></a>
### Method 5: Direct SMTP Send

As of Fossil 2.7, the code to support this method is incomplete, so you
cannot currently select it as an option in Admin → Notification.


<a id="uvs"></a>
## Users vs Subscribers

Fossil makes a distinction between "users" and "subscribers".  A user is
someone with a username and password: that is, someone who can log into
the Fossil repository.  A subscriber is someone who receives email
alerts.  Users can also be subscribers and subscribers can be users, but
that does not have to be the case.  It is possible to be a user without
being a subscriber and to be a subscriber without being a user.

In the repository database file, users are tracked with the `user` table
and subscribers are tracked via the `subscriber` table.


<a id="admin"></a>
## Administrator Activities

The "[List Subscribers](/subscribers)" button at the top of the Admin →
Notification screen gives a list of subscribers, which gives a Fossil
server administrator a lot of power over those subscriptions.

Clicking an email address in this subscriber list opens the same
`/alerts` page that the user can see for their own subscription, but
with more information and functionality than normal users get:

*  Subscription creation and modification timestamps.

*  The IP address the user had when they last made a change via either
   `/subscribe` or `/alert`.

*  The user's login name, if they are not [a mere subscriber](#uvs).  A
   Fossil Admin user is allowed to modify this, either to tie a
   subscription-only record to an existing Fossil user account or to
   break that tie.

*  The "Do not call" checkbox allows a Fossil Admin user to mark a given
   email address so that Fossil never sends email to that address.  This
   is distinct from unsubscribing that email address because it prevents
   Fossil from accepting a new subscription for that address.

*  The Verified checkbox is initially unchecked for subscriber-only
   email addresses until the user clicks the link in the verification
   email. This checkbox lets the Fossil Admin user manually verify the
   user, such as in the case where the verification email message got
   lost.  Unchecking this box does not cause another verification email
   to be sent.

*  Admin users (only) may activate the "user elevation" subscription,
   which sends a notification when a user is created or is explicitly
   assigned permission they did not formerly have.

This screen also allows a Fossil Admin user to perform other activities
on behalf of a subscriber which they could do themselves, such as to
[unsubscribe](#unsub) them.


<a id="backup"></a>
## Cloning, Syncing, and Backups

That’s [covered elsewhere](./backup.md#alerts).


<a id="pages" name="commands"></a>
## Controlling the Email Alert System

This section collects the list of Fossil UI pages and CLI commands that
control the email alert system, some of which have not been mentioned so
far:

Commands:

   *  The [`alerts`](/help?cmd=alerts) command
   *  The [`test-alert`](/help?cmd=test-alert) command
   *  The [`test-add-alerts`](/help?cmd=test-add-alerts) command

Web pages available to users and subscribers:

   *  The [`/subscribe`](/help?cmd=/subscribe) page
   *  The [`/alerts`](/help?cmd=/alerts) page
   *  The [`/unsubscribe`](/help?cmd=/unsubscribe) page
   *  The [`/renew`](/help?cmd=/renew) page
   *  The [`/contact_admin`](/help?cmd=/contact_admin) page

Administrator-only web pages:

   *  The [`/setup_notification`](/help?cmd=/setup_notification) page
   *  The [`/subscribers`](/help?cmd=/subscribers) page


<a id="design"></a>
## Design of Email Alerts

This section describes the low-level design of the email alert system in
Fossil.  This expands on the high-level administration focused material
above with minimal repetition.

This section assumes expert-level systems knowledge. If the material
above sufficed for your purposes, feel free to skip this section, which
runs to the end of this document.


<a id="datades"></a>
### Data Design

There are two new tables in the repository database, starting with
Fossil 2.7.  These tables are not created in new repositories by
default.  The tables only come into existence as needed when email
alerts are configured and used.

  *  <b>SUBSCRIBER</b> →
     The subscriber table records the email address for people who
     want to receive email notifications.  Each subscriber has a
     `subscriberCode` which is a random 32-byte blob that uniquely
     identifies the subscriber.  There are also fields to indicate
     what kinds of notifications the subscriber wishes to receive,
     whether or not the email address of the subscriber has been
     verified, etc.

  *  <b>PENDING\_ALERT</b> →
     The PENDING\_ALERT table contains records that define events
     about which alert emails might need to be sent.
     A pending\_alert always refers to an entry in the
     EVENT table.  The EVENT table is part of the standard schema
     and records timeline entries.  In other words, there is one
     row in the EVENT table for each possible timeline entry.  The
     PENDING\_ALERT table refers to EVENT table entries for which
     we might need to send alert emails.

As pointed out above, ["subscribers" are distinct from "users"](#uvs).
The SUBSCRIBER.SUNAME field is the optional linkage between users and
subscribers.


<a id="stdout"></a>
### The "stdout" Method

The [list of mail sending methods](#advanced) above left out an
internal-only method called "stdout" which simply writes the text of the
email message on standard output.  The "stdout" method is used for
testing and debugging.  If you need something similar and can't modify
your local Fossil instance to use this method, you might temporarily
switch to [the "dir" method](#dir) instead.


<a id="msgfmt"></a>
### Message Format

The email messages generated by Fossil have a [well-formed
header][rfc822].  The downstream processing is expected to extract the
"To:", "From:", "Subject:" and whatever other attributes it needs from
the email header text.

These emails use the `text/plain` MIME type with the UTF-8 character
set.  We currently use a transfer encoding of `quoted-printable`, but
there is commented-out code in Fossil to switch to `base64` encoding,
which Fossil used in the early days leading up to the 2.7 release.

If you switch Fossil back to `base64` mode, you may want to build a
utility program that ships in the Fossil source tree named
["tools/decode-email.c"](/file/tools/decode-email.c) which can decode
these messages into a human-readable format.


<a id="inbound" name="bounces"></a>
### Dealing with Inbound Email

Inbound email messages — for example, bounces from failed alert emails —
should be relayed to the `fossil email inbound` command.  That command
is currently a no-op place-holder.  At some point, we will need to
design and write a bounce-message processing system for Fossil.


<a id="password" name="scode" name="verification"></a>
### Passwords vs Subscriber Codes

When anonymous passers-by on the Internet sign up for email alerts,
their email address must first be verified.  An email message is sent to
the address supplied inviting the user to click on a link.  The link
includes a pseudorandom 128-bit blob encoded as 32 hexadecimal digits,
which serves in place of a password for that email address.  (This is
stored in the database as `subscriber.subscriberCode`.) If anyone visits
the link, the email address is verified.

Knowledge of the `subscriberCode` is sufficient to control a
subscription.

Because this code is included in plain text in email alert messages, it
is not as secure as a separate password, but it has several virtues:

*   It is easier for the average subscriber to deal with in that they
    don't have to come up with yet another password and store it safely.

*   If the `subscriberCode` is stolen, the worst that can happen is that
    the thief can change that email address's subscription settings.
    Contrast a password which may be shared with other services, which
    then compromises those other services.

*   No PII other than the subscriber's email address is available to an
    attacker with the `subscriberCode`.  Nor can knowledge of the
    `subscriberCode` lead to an email flood or other annoyance attack, as
    far as I can see.

If the `subscriberCodes` for a Fossil repository are ever compromised,
new ones can be generated as follows:

    UPDATE subscriber SET subscriberCode=randomblob(32);

Since this then affects all new email alerts going out from Fossil, your
end users may never even realize that they're getting new codes, as long
as they don't click on the URLs in the footer of old alert messages.

With that in mind, a Fossil server administrator could choose to
randomize the `subscriberCodes` periodically, such as just before the
daily digest emails are sent out each day.

**Important:** All of the above is distinct from the passwords for users
with a Fossil repository login. Such users also have subscriber codes,
but those codes can only be used to modify the user's email alert
settings. That code cannot allow a user to log into the user's Fossil
repository account.


<a id="processing"></a>
### Internal Processing Flow

Almost all of the email alert code is found in the
[`src/alerts.c`](/file/src/alerts.c) source file.

When email alerts are enabled, a trigger is created in the schema
(`email_trigger1`) that adds a new entry to the `PENDING_ALERT` table
every time a row is added to the `EVENT` table.  During a 
`fossil rebuild`, the `EVENT` table is rebuilt from scratch; since we do not
want users to get alerts for every historical check-in, the trigger is
disabled during `rebuild`.

Email alerts are sent out by the `alert_send_alerts()` function, which
is normally called automatically due to the `email-autoexec` setting,
which defaults to enabled. If that setting is disabled or if the user
simply wants to force email alerts to be sent immediately, they can give
a `fossil alert send` command, such as via a `cron` script.  Each time
this function is called, the alert messages are moved further down the
chain, so you cannot cause duplicate alerts by calling it too often.

Digests are handled by recording the time of the last digest in the
`email-last-digest` setting, and only sending a new digest if the
current time is one day or later after the last digest.

Individual emails are sent to each subscriber.  I (drh) ran tests and
found that I could send about 1200 emails/second, which is fast enough
that I do not need to resort to trying to notify multiple subscribers
with a single email.  Because each subscriber gets a separate email, the
system can include information in the email that is unique to the
subscriber, such as a link to the page to edit their subscription.  That
link includes the `subscriberCode`.
