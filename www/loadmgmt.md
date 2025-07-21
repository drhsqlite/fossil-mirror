# Managing Server Load

A Fossil server is very efficient and normally presents a very light
load on the server.  The Fossil [self-hosting server][sh] is a 1/24th
slice VM at [Linode.com][lin] hosting 65 other repositories in addition
to Fossil, including some very high-traffic sites such as
<http://www.sqlite.org> and <http://system.data.sqlite.org>. This small
VM has a typical load of 0.05 to 0.1. A single HTTP request to Fossil
normally takes less than 10 milliseconds of CPU time to complete, so
requests can be arriving at a continuous rate of 20 or more per second,
and the CPU can still be mostly idle.

However, there are some Fossil web pages that can consume large amounts
of CPU time, especially on repositories with a large number of files or
with long revision histories.  High CPU usage pages include
[`/zip`](/help/zip), [`/tarball`](/help/tarball),
[`/annotate`](/help/annotate), and others.  On very large repositories,
these commands can take 15 seconds or more of CPU time.  If these kinds
of requests arrive too quickly, the load average on the server can grow
dramatically, making the server unresponsive.

Fossil provides two capabilities to help avoid server overload problems
due to excessive requests to expensive pages:

1.  An optional cache is available that remembers the 10 most recently
    requested `/zip` or `/tarball` pages and returns the precomputed
    answer if the same page is requested again.

2.  Page requests can be configured to fail with a
    “[503 Server Overload][503]” HTTP error if any request is
    received while the host load average is too high.

Both of these load-control mechanisms are turned off by default, but
they are recommended for high-traffic sites. Users with [admin
permissions](caps/index.md) are exempt from these restrictions,
provided they are logged in before the load gets too high (login is
disabled under high load).

The webpage cache is activated using the [`fossil cache init`](/help/cache)
command-line on the server.  Add a `-R` option to
specify the specific repository for which to enable caching.  If running
this command as root, be sure to “`chown`” the cache database to give
the Fossil server write permission for the user ID of the web server;
this is a separate file in the same directory and with the same name as
the repository but with the “`.fossil`” suffix changed to “`.cache`”.

To activate the server load control feature visit the Admin → Access
setup page in the administrative web interface; in the “**Server Load
Average Limit**” box enter the load average threshold above which “503
Server Overload” replies will be issued for expensive requests.  On the
self-hosting Fossil server, that value is set to 1.5, but you could
easily set it higher on a multi-core server.

The maximum load average can also be set on the command line using
commands like this:

    fossil setting max-loadavg 1.5
    fossil all setting max-loadavg 1.5

The second form is especially useful for changing the maximum load
average simultaneously on a large number of repositories.

Note that this load-average limiting feature is only available on
operating systems that support the [`getloadavg()`][gla] API.  Most
modern Unix systems have this interface, but Windows does not, so the
feature will not work on Windows.

Because Linux implements `getloadavg()` by accessing the `/proc/loadavg`
virtual file, you will need to make sure `/proc` is available to the
Fossil server. The most common reason for it to not be available is that
you are running a Fossil instance [inside a `chroot(2)`
jail](./chroot.md) and you have not mounted the `/proc` virtual file
system inside that jail. On the [self-hosting Fossil repositories][sh],
this was accomplished by adding a line to the `/etc/fstab` file:

    chroot_jail_proc /home/www/proc proc ro 0 0

The `/home/www/proc` pathname should be adjusted so that the `/proc`
component is at the root of the chroot jail, of course.

To see if the load-average limiter is functional, visit the
[`/test-env`][hte] page of the server to view the current load average.
If the value for the load average is greater than zero, that means that
it is possible to activate the load-average limiter on that repository.
If the load average shows exactly "0.0", then that means that Fossil is
unable to find the load average. This can either be because it is in a
`chroot(2)` jail without `/proc` access, or because it is running on a
system that does not support `getloadavg()` and so the load-average
limiter will not function.


[503]: http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html#sec10.5.4
[hte]: /help?cmd=/test-env
[gla]: https://linux.die.net/man/3/getloadavg
[lin]: http://www.linode.com
[sh]:  ./selfhost.wiki
