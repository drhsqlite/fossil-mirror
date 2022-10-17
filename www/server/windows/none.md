# Serving as a Standalone Server on Windows

On Windows, this method works more or less identically to how it’s
documented in [the generic instructions](../any/none.md).

...but only while `fossil.exe` is actually running, which is the source
of much trouble on Windows. This problem has two halves:


## No App Startup Without Desktop

The easy methods for starting a program in Windows at system start all
require an interactive desktop.  There is no *easy* way to start an arbitrary
program on Windows at boot before anyone has logged in. In Unix
terms, Windows has no simple equivalent to [the `/etc/rc.local` file][rcl].

You can partially get around the first problem by setting your `fossil
server` call up as one of the user’s interactive startup items. Windows
10 has its own [idiosyncratic way of doing this][si10], and in older
systems you have [several alternatives to this][si7]. Regardless of the
actual mechanism, these will cause the Fossil standalone HTTP server to
start on an *interactive desktop login* only. While you’re sitting at
the Windows login screen, the Fossil server is *down*.

[rcl]:  http://nixdoc.net/man-pages/FreeBSD/man8/rc.local.8.html
[si10]: https://www.tenforums.com/tutorials/2944-add-delete-enable-disable-startup-items-windows-10-a.html
[si7]:  https://www.wikihow.com/Change-Startup-Programs-in-Windows-7



## No Simple Background Mode

Windows also lacks a direct equivalent of the Bourne shell’s “`&`” control operator to
run a program in the background, which you can give in Unix’s `rc.local`
file, which is just a normal Bourne shell script.

By “background,” I mean
“not attached to any interactive user’s login session.” When the
`rc.local` script exits in Unix, any program it backgrounded *stays
running*. There is no simple and direct equivalent to this mechanism in
Windows.

If you set `fossil server` to run on interactive login, as above, it
will shut right back down again when that user logs back out.

With Windows 10, it’s especially problematic because you can no longer
make the OS put off updates arbitrarily: your Fossil server will go down
every time Windows Update decides it needs to reboot your computer, and
then Fossil service will *stay* down until someone logs back into that
machine interactively.


## Better Solutions

Because of these problems, we only recommend setting `fossil server` up
on Windows this way when
you’re a solo developer or you work in a small office where everyone
arrives more or less at the same time each day, and everyone goes home
about the same time each day, so that one user can keep the Fossil
server up through the working day.

If your needs go at all beyond this, you should expect proper “server”
behavior, which you can get on Windows by [registering Fossil as a
Windows service](./service.md), which solves the interactive startup and
shutdown problems above, at a bit of complexity over the Startup Items
method. You may also want to consider putting that service behind [an
IIS front-end proxy](./iis.md) to add additional web serving features.

*[Return to the top-level Fossil server article.](../)*
