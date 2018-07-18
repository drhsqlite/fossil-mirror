Design of Email Notification
============================

This document contains high-level design notes for the email
notification system in Fossil.  Use this document to get a better
understanding of how Fossil handles email notification, to help
with doing custom configurations, or to help contribute features.

This document assumes expert-level systems knowledge.  A separate
tutorial for setting up email notification by non-experts will be
generated once the email notification system stabilizes.

Email notification is under active development as of this writing
(2018-06-25).  Check back frequently for updates.

Data Design
-----------

There are three new tables in the repository database.  These tables
are not created in new repositories by default.  The tables only
come into existance if email notification is configured and used.


  *  <b>SUBSCRIBER</b> &rarr;
     The subscriber table records the email address for people who
     want to receive email notifications.  Each subscriber has a
     "subscriberCode" which is a random 32-byte blob that uniquely
     identifies the subscriber.  There are also fields to indicate
     what kinds of notifications the subscriber wishes to receive,
     whether or not the email address of the subscriber has been
     verified, etc.

  *  <b>PENDING\_ALERT</b> &rarr;
     The PENDING\_ALERT table contains records that define events
     about which notification emails might need to be sent.
     A pending\_alert always refers to an entry in the
     EVENT table.  The EVENT table is part of the standard schema
     and records timeline entries.  In other words, there is one
     row in the EVENT table for each possible timeline entry.  The
     PENDING\_ALERT table refers to EVENT table entries for which
     we might need to send notification emails.

  *  <b>EMAIL\_BOUNCE</b> &rarr;
     This table is intended to record email bounce history so that
     subscribers with excessive bounces can be turned off.  That
     logic has not yet been implemented so the EMAIL\_BOUNCE table
     is currently unused.

Note that "subscribers" are distinct from "users" in the USER table.
A "user" is someone who has a login and password.  A "subscriber" is
an email address that receives notification events.  Users can be
subscribers, and there is a SUBSCRIBER.SUNAME field that records
the linkage between users and subscribers.  But it is also possible
to be a user without being a subscriber, or to be a subscriber without
being a user.

Sending Email Messages
----------------------

Fossil expects to interact with an external mail agent.
There are currently three different methods for sending outbound
email messages from Fossil to the external mail agent:

  1.  <b>"pipe"</b> &rarr; Invoke an external command that accepts
      the email message on standard input.  This is useful if the
      host computer has a command like /usr/sbin/sendmail that will
      accept well-formed email messages from standard input and forward
      them to the appropriate destination.

  2.  <b>"db"</b> &rarr; Write outgoing email messages into an
      SQLite database file.  The self-hosting Fossil website uses
      this technique because Fossil runs inside a reduced-privilege
      chroot jail and cannot invoke commands like /usr/sbin/sendmail.
      A separate TCL script running outside of the jail monitors
      the email queue database and forwards email messages to the
      Postfix mail transfer agent.  There is an example TCL script in the
      [tools/email-sender.tcl](/file/tools/email-sender.tcl) file
      of the source tree that shows how this is done.

  3.  <b>"dir"</b> &rarr; Write outgoing email messages as individual
      files in a designated directory.  This might be useful for
      testing and debugging.

Internally, there is a fourth email sending method named "stdout"
which simply writes the text of the email message on standard output.
The "stdout" method is used for testing and debugging.

Perhaps we will add an "smtp" sending method in the future.  The
main problem with an "smtp" delivery method is that front-line Fossil
running inside the privilege jail would need to deal with all kinds
of errors from SMTP, such as unable to connect, or connection resets,
etc.  SMTP expects the sender to have the ability to retry, does it
not?

The emails transmitted have a well-formed header.  The downstream
processing is expected to extract the "To:", "From:", "Subject:" and
whatever other attributes it needs from the email header text.

All emails are text/plain and use a transfer-encoding of base64.

There is a utility command-line program named 
["tools/decode-email.c"](/file/tools/decode-email.c) in
the Fossil source tree.  If you compile this program, you can use it
to convert the base64 transfer-encoding into human-readable output for
testing and debugging.

Receiving Email Messages
------------------------

Inbound email messages (for example bounces from failed notification
emails) should be relayed to the "fossil email inbound" command.  That
command is currently a no-op place-holder.  At some point, we will need
to design and write a bounce-message processing system for Fossil.

Controlling The Setup
---------------------

Commands:

   *  The [email](/help?cmd=email) command

Web pages:

   *  The [/subscribe](/help?cmd=/subscribe) page
   *  The [/alerts](/help?cmd=/alerts) page
   *  The [/unsubscribe](/help?cmd=/unsubscribe) page
   *  The [/msgtoadmin](/help?cmd=/msgtoadmin) page

Web pages for administrators only:

   *  The [/setup_email](/help?cmd=/setup_email) page
   *  The [/subscribers](/help?cmd=/subscribers) page

Test command:

   *  The [test-alert](/help?cmd=test-alert) command
   *  The [test-add-alerts](/help?cmd=test-add-alerts) command

Email Address Verification
--------------------------

When anonymous passers-by on the internet sign up for email notifications,
their email address must first be verified.  An email message is sent to
the address supplied inviting the user to click on a link.  The link includes
the random 32-byte subscriberCode in hex.  If anyone visits the link, the
email address is verified.

There is no password.  Knowledge of the subscriberCode is sufficient to
control the subscription.  This is not a secure as a separate password,
but on the other hand it is easier for the average subscriber to deal
with in that they don't have to come up with yet another password.  Also,
even if the subscriberCode is stolen, the worst that can happens is that
the thief can change your subscription settings.  No PII (other than
the subscriber's email address) is available to an attacker with the
subscriberCode.  Nor can knowledge of the subscriberCode lead to a
email flood or other annoyance attack, as far as I can see.

If subscriberCodes are ever compromised, new ones can be generated
as follows:

>   UPDATE subscriber SET subscriberCode=randomblob(32);

Perhaps the system be enhanced to randomize the
subscriberCodes periodically - say just before each daily digest
is sent out?

User Control Of Their Subscription
----------------------------------

If a user has a separate account with a login and password for
the repository, then their subscription is linked to their account.
On the /login page is a link to a page to control their subscription.

For users without logins, they can request a link to a page for
controling their subscription on the /alerts or /unsubscribe page.
The link is sent via email, and includes the subscriberCode.

Internal Processing Flow
------------------------

Almost all of the email notification code is found in the "src/email.c"
source file.

When email notifications are enabled, a trigger is created in the schema
(the "email_trigger1" trigger) that adds a new entry to the
PENDING_ALERT table every time a row is added to the EVENT table.
During a "rebuild", the EVENT table is rebuilt from scratch, and we
do not want users to get notifications for every historical check-in,
so the trigger is disabled during "rebuild".

Email notifications are sent out by the email_send_alerts() function.
This function is can be called by having a cron job invoke the
"fossil email exec" command.  Or, if the email-autoexec setting is
enabled, then email_send_alerts() is invoked automatically after each
successful webpage is generated.  The latter approach is used on the
Fossil self-hosting repository.  The email_send_alerts() function is
a no-op (obviously) if there are no pending events to be sent.

Digests are handled by recording the time of the last digest in the
"email-last-digest" setting, and only sending a new digest if the
current time is one day or later after the last digest.

Individual emails are sent to each subscriber.  I ran tests and found
that I could send about 1200 emails/second, which is fast enough so that
I do not need to resort to trying to notify multiple subscribers with
a single email.  Because each subscriber gets a separate email, the
system can include information in the email that is unique to the
subscriber, such as a link to the page to edit their subscription.  That
link includes the subscriberCode.

Other Notes
-----------

The "fossil configuration pull subscriber" command pulls down the content
of the SUBSCRIBER table.  This is intended to as a backup-only.  It
is not desirable to have two or more systems sending emails to the
same people for the same repository, as that would mean users would
receive duplicate emails.  Hence the settings that control email 
notifications are not transmitted with the pull.  The "push", "export",
and "import" commands all work similarly
