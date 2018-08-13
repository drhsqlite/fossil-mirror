Email Alerts
============

The email alert system is a work-in-progress.
This documentation was last updated on 2018-08-12.
Check back later for updates.

Email Alerts And Notifications
------------------------------

Beginning with version 2.7, Fossil supports the ability to send
email messages to subscribers alerting them to changes in the repository.
Subscribers can request an email notification of the following kinds
of changes:

  *  New check-ins
  *  Changes to any ticket
  *  Changes to any wiki page
  *  New forum posts
  *  Announcements

Subscribers can either elect to receive emails as soon as these events happen,
or they can receive a daily digest of the events instead.

Email alerts are sent by a [Fossil server](./server.wiki).  You must
have a server [set up to make use of email alerts](#setup).  Email
alerts do not currently work if you are only using Fossil from the
command line.

Users and Subscribers
---------------------

Fossil makes a distinction between "users" and "subscribers".  A
user is someone with a username and password — someone who can
log in.  A subscriber is someone who receives email alerts.  Users
can also be subscribers and subscribers can be users, but that does
not have to be the case.  It is possible to be a user without being
a subscriber and to be a subscriber without being a user.

In the repository database file, users are tracked with the USER table
and subscribers are tracked via the SUBSCRIBER table.

<a id="setup"></a>
Activating Email Alerts
-----------------------

Email alerts are turned off by default.  To activate them, log into
the Fossil server as an administrator and visit the 
[Admin/Notification](/setup_notification)
setup page. ([`/setup_notification`](/setup_notification))

Important:  Email alerts are configured using Admin/Notification, not
Admin/Email-Server.  The Email-Server setup screen is used to configure
a different subsystem within Fossil.

The Admin/Notification setup screen lets you configure how Fossil should
send email for alerts.  There are some required fields at the top of the
screen for elements such as the "From:" address for outgoing emails,
the URL for the Fossil server, and a nickname for the repository that
will appear in the "Subject:" line of outgoing emails.  But the key
setup parameter is the "Email Send Method".

Fossil supports multiple methods for sending email alerts:

  1.  Pipe the email message text into a command, such as `sendmail`.
  2.  Store email messages as individual files in a directory and let
      some other process set up by the administrator take care of
      reading and forwarding those files.
  3.  Store email messages as entries in an SQLite database where
      some external process and read and forward the emails.
  4.  Send emails to an SMTP Relay.
  5.  Send emails directly to the recipients via SMTP.

As of 2018-08-08, method (5) is not yet supported, but there are plans
to add support soon.

The self-hosting Fossil repository at <https://www.fossil-scm.org/> currently
uses method (3).  Outgoing email messages are added to an SQLite database
file.  A separate daemon process continously monitors that database file,
extracts email messages as they are added, and hands them off to 
"procmail" to be sent on to the final recipient.  The self-hosting
Fossil repository uses this technique rather than method (1) because
it is running inside of a restrictive chroot jail which is unable to
hand off messages to "procmail" directly.  The daemon that monitors the
email database is a [short TCL script](/file/tools/email-sender.tcl).
That daemon is started automatically by adding this line:

      /usr/bin/tclsh /home/www/fossil/email-sender.tcl &

To the `/etc/rc.local` file of the Ubuntu server that hosts the
repository.

After making necessary changes to the Admin/Notification page, test
those changes by clicking the "[Send Announcement](/announce)" link
at the top of the page.  Fill in your email address in the "To:"
line and a test message below, and press "Send Message" to verify that
outgoing email is working.

<a id="cap7"></a>
Once email notification is working, one must also adjust user permissions
to allow users to subscribe to email notification.  On the 
Setup/User page, under the permissions for each user, is a new capability
called "Email Alerts".  The corresponding capability letter is "7".
That new "7" capability must be enabled in order for
users to be able to become subscribers.  To allow anonymous passers-by
on the internet to subscribe, simply enable "Email Alerts" for the
"nobody" user category. To require that the user solve a simple CAPTCHA
first, add it to the "anonymous" user category instead.

Signing Up For Email Notification
---------------------------------

Users and/or anonymous passers-by can visit the 
[`/subscribe`](/subscribe) page to sign
up for email notification.

If your users are getting the following complaint from Fossil:

<blockquote>
  Use a different login with greater privilege than FOO to access
  /subscribe
</blockquote>

...then you forgot to [give capability 7](#cap7) to that user or to a
user category that the user is a member of.

After signing up, a single verification email
is sent.  The new subscriber must click a link on that email in order to
activate the subscription.

Subscription verification emails are only sent once.  This is a defense
against malicious robots that try to harass innocent internet users
by having subscription pages send multiple verification emails.
If the initial subscription verification does not go through correctly,
an administrator must intervene to reset the subscription.

Every subscriber has a long random hexadecimal security code that serves
as their password.  All email notifications contain a link back to the
Fossil server, incorporating this security code, which allows the 
subscriber to adjust their subscription options.

Administrator Activities
------------------------

The repository administrator has unlimited control over individual
subscriptions.  The "[List Subscribers](/subscribers)" button at the top
of the Setup/Notification screen gives a list of subscribers.  Clicking on
any subscriber link allows the administrator to adjust the subscription.

To unsubscribe, first select the "unsubscribe" checkbox, then press the
"Unsubscribe" button.

The "verified" checkbox determines whether or not an email address has
been verified.  This can be enabled or disabled manually by the
administrator.

Cloning, Syncing, and Backups
-----------------------------

The Setup/Notification settings are not replicated using clone or sync.
In a network of peer repositories, you only want one repository sending
email notifications.  If you were to replicate the email notification
settings to a separate repository, then subscribers would get multiple
notifications for each event, which would be bad.

However, the subscriber list can be synced for backup purposes.  Use the
[`fossil config pull subscriber`](/help?cmd=configuration) command to
pull the latest subscriber list from a server into a backup repository.
