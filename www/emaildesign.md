Design of Email Notification
============================

This document contains high-level design notes for the email
notification system in Fossil.  Use this document to get a better
understanding of how Fossil handles email notification, to help
with doing custom configurations, or to help contribute features.

This document assumes expert-level systems knowledge.  A separate
tutorial for setting up email notification by non-experts will be
generated once the email notification systems stablizes.

Email notification is under active development as of this writing
(2018-06-23).  Check back frequently for updates.

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
      SQLite database file.  The self-hosting Fossil website will
      probably use this technique because Fossil runs inside a
      reduced-privilege chroot jail and cannot invoke commands
      like /usr/sbin/sendmail.  A separate TCL script running on
      the outside of the jail monitors the database and forwards
      email messages to the Postfix mail transfer agent.  There is
      an example TCL script in the
      [tools/email-monitor.tcl](/file/tools/email-monitor.tcl) file
      of the source tree that shows how to do this.

  3.  <b>"dir"</b> $rarr; Write outgoing email messages as individual
      files in a designated directory.  This might be useful for
      testing and debugging.

Internally, there is a fourth email sending method named "stdout"
which simply writes the text of the email message on standard output.
The "stdout" method is used for testing and debugging.

Perhaps we will add an "smtp" sending method in the future.

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
