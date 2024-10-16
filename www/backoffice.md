Backoffice
==========

This is technical documentation about the internal workings of Fossil.
Ordinary Fossil users do not need to know about anything covered by this
document.  The information here is intended for people who want to enhance
or extend the Fossil code, or who just want a deeper understanding of
the internal workings of Fossil.

What Is The Backoffice
----------------------

The backoffice is a mechanism used by a
[Fossil server](./server/) to do low-priority
background work that is not directly related to the user interface.  Here
are some examples of the kinds of work that backoffice performs:

  1.  Sending email alerts and notifications
  2.  Sending out daily digests of email notifications
  3.  Other background email handling chores
  4.  Automatic syncing of peer repositories
  5.  Repository maintenance and optimization

(As of 2018-08-07, only items 1 and 2 have actually been implemented.)
The idea is that the backoffice handles behind-the-scenes work that does
not have tight latency requirements.

When Backoffice Runs
--------------------

A backoffice process is usually launched automatically by a webpage
request.  After each webpage is generated, Fossil checks to see if any
backoffice work needs to be done. If there is work to do, and no other
process is already assigned to do the work, then a new backoffice process
is started to do the work.

This happens for every webpage, regardless of how that webpage is launched,
and regardless of the purpose of the webpage.  This also happens on the
server for "[fossil sync](/help?cmd=sync)" and
[fossil clone](/help?cmd=clone)" commands which are implemented as
web requests - albeit requests that the human user never sees.
Web requests can arrive at the Fossil server via direct TCP/IP (for example
when Fossil is started using commands like "[fossil server](/help?cmd=server)")
or via [CGI](./server/any/cgi.md) or
[SCGI](./server/any/scgi.md) or via SSH.
A backoffice process might be started regardless of the origin of the
request.

The backoffice is not a daemon.  Each backoffice process runs for a short
while and then exits.  This helps keep Fossil easy to manage, since there
are no daemons to start and stop.  To upgrade Fossil to a new version,
you simply replace the older "fossil" executable with the newer one, and
the backoffice processes will (within a minute or so) start using the new
one.  (Upgrading the executable on Windows is more complicated, since on
Windows it is not possible to replace an executable file that is in active
use.  But Windows users probably already know this.)

The backoffice is serialized and rate limited.  No more than a single
backoffice process will be running at once, and backoffice runs will not
occur more frequently than once every 60 seconds.  (The 60-second spacing
is controlled by the BKOFCE_LEASE_TIME macro in the 
[backoffice.c](/file/src/backoffice.c) source file.)

If a Fossil server is idle, then no backoffice processes will be running.
That means there are no extra processes sitting around taking up memory
and process table slots for seldom accessed repositories.
The backoffice is an on-demand system.
A busy repository will usually have a backoffice
running at all times.  But an infrequently accessed repository will only have
backoffice processes running for a minute or two following the most recent
access.

Manually Running The Backoffice
-------------------------------

The automatic backoffice runs are sufficient for most installations.
However, the daily digest of email notifications is handled by the
backoffice.  If a Fossil server can sometimes go more than a day without
being accessed, then the automatic backoffice will never run, and the
daily digest might not go out until somebody does visit a webpage.
If this is a problem, an administrator can set up a cron job to
periodically run:

    fossil backoffice _REPOSITORY_

That command will cause backoffice processing to occur immediately.
Note that this is almost never necessary for an internet-facing
Fossil repository, since most repositories will get multiple accesses
per day from random robots, which will be sufficient to kick off the
daily digest emails.  And even for a private server, if there is very
little traffic, then the daily digests are probably a no-op anyhow
and won't be missed.

Automatic Backoffice Does Not Work On Some Systems
--------------------------------------------------

We have observed that the automatic backoffice does not work on
some system - OpenBSD in particular.  We still do not understand why
this is.  (If you have insights, please share them on the
[Fossil Forum](https://fossil-scm.org/forum) so that we can perhaps
fix the problem.)  For now, the backoffice must be run manually
on OpenBSD systems.

To set up fully-manual backoffice, first disable the automatic backoffice
using the "[backoffice-disable](/help?cmd=backoffice-disable)" setting.

    fossil setting backoffice-disable on

Then arrange to invoke the backoffice separately using a command
like this:

    fossil backoffice --poll 30 _REPOSITORY-LIST_

Multiple repositories can be named.  This one command will handle
launching the backoffice for all of them.  There are additional useful
command-line options.  See the "[fossil backoffice](/help?cmd=backoffice)"
documentation for details.

The backoffice processes run manually using the "fossil backoffice"
command do not normally use a lease.  That means that if you run the
"fossil backoffice" command with --poll and you forget to disable
automatic backoffice by setting the "backoffice-disable" flag, then
you might have one backoffice running due to a command and another due
to a webpage access, both at the same time.  This is harmless.  The
only downside is that it uses extra CPU time.

How Backoffice Is Implemented
-----------------------------

The backoffice is implemented by the 
"[backoffice.c](/file/src/backoffice.c)" source file.

Serialization and rate limiting is handled by a single entry in the
repository database CONFIG table named "backoffice".  This entry is
called "the lease".  The value of the lease
is a text string representing four integers, which
are respectively:

  1.  The process id of the "current" backoffice process
  2.  The lease expiration time of the current backoffice process
  3.  The process id of the "next" backoffice process
  4.  The lease expiration time for the next backoffice process

Times are expressed in seconds since 1970.  A process id of zero means
"no process".  Sometimes the process id will be non-zero even if there
is no corresponding process. Fossil knows how to figure out whether or
not a process still exists.

You can print out a decoded copy of the current backoffice lease using
this command:

    fossil test-backoffice-lease -R _REPOSITORY_

If a system has been idle for a long time, then there will be no
backoffice processes.  (Either the process id entries in the lease
will be zero, or there will exist no process associated with the
process id.) When a new web request comes in, the system
sees that no backoffice process is active and so it kicks off a separate
process to run backoffice.

The new backoffice process becomes the "current" process.  It sets a
lease expiration time for itself to be 60 seconds in the future.
Then it does the backoffice processing and exits.  Note that usually
the backoffice process will exit long before its lease expires.  That
is ok.  The lease is there to limit the rate at which backoffice processes
run.

If a new backoffice process starts up and sees that the "current" lease has
yet to expire, the new process makes itself the "next" backoffice process
and sets its expiration time to be 60 seconds past the expiration time of
the "current" backoffice process.  The "next" process then puts itself to
sleep until the "current" lease expires.  After the "current"
lease expires and the "current" process has itself exited, then
the "next" process promotes itself to the new "current" process.  It
sets the current lease expiration to be 60 seconds in the future, runs
whatever backoffice work is needed, then exits.

If a new backoffice process starts up and finds that there is already
a "current" lease and a "next" process, it exits without doing anything.
This should happen only rarely, since the lease information is checked
prior to spawning the backoffice process, so a conflict will only happen
in a race.

Because the "backoffice" entry of the CONFIG table is in the repository
database, access to the lease is serialized.  The lease prevents more
than one backoffice process from running at a time.  It prevents
backoffice processes from running more frequently than once every 60 seconds.
And, it guarantees (assuming processes are not killed out-of-band) that
every web request will be followed within 60 seconds by a backoffice
run.

Debugging The Backoffice
------------------------

The backoffice should "just work".  It should not require administrator
attention.  However, if you suspect that something is not working right,
there are some debugging aids.

We have already mentioned the command that shows the backoffice lease
for a repository:

    fossil test-backoffice-lease -R _REPOSITORY_

Running that command every few seconds should show what is going on with
backoffice processing in a particular repository.

There are also settings that control backoffice behavior.  The
"backoffice-nodelay" setting prevents the "next" process from taking a
lease and sleeping.  If "backoffice-nodelay" is set, that causes all
backoffice processes to exit either immediately or after doing whatever
backoffice works needs to be done.  If something is going wrong and
backoffice leases are causing delays in webpage processing, then setting
"backoffice-nodelay" to true can work around the problem until the bug
can be fixed.  The "backoffice-logfile" setting is the name of a log
file onto which is appended a short message every time a backoffice
process actually starts to do the backoffice work.  This log file can
be used to verify that backoffice really is running, if there is any
doubt.  The "backoffice-disable" setting prevents automatic backoffice
processing, if true.  Use this to completely disable backoffice processing
that occurs automatically after each HTTP request.  The "backoffice-disable"
setting does not affect the operation of the manual
"fossil backoffice" command.
Most installations should leave "backoffice-nodelay" and "backoffice-disable"
set to their default values of off and
leave "backoffice-logfile" unset or set to an empty string.
