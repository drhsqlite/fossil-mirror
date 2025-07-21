# The Server Chroot Jail

If you run Fossil as root in any mode that [serves data on the
network][srv], and you're running it on Unix or a compatible OS, Fossil
will drop itself into a [`chroot(2)` jail][cj] shortly after starting
up. The usual reason for launching Fossil 
as root to allow it to bind to TCP port 80 for HTTP
service, since normal users are restricted to ports 1024 and higher.

Fossil uses the owner of the Fossil repository file as its new user
ID when it drops root privileges.

When Fossil enters a chroot jail, it needs to have all of its dependencies
inside the chroot jail in order to continue work.  There are several
resources that need to be inside the chroot jail with Fossil in order for
Fossil to work correctly:

*   the repository file(s)

*   `/dev/null` — create it with `mknod(8)` inside the jail directory
    ([Linux example][mnl], [OpenBSD example][obsd])

*   `/dev/urandom` — ditto

*   `/proc` — you might need to mount this virtual filesystem inside the
    jail on Linux systems that make use of [Fossil’s server load
    shedding feature][fls]

*   any shared libraries your `fossil` binary is linked to, unless you
    [configured Fossil with `--static`][bld] to avoid it

Fossil does all of this as one of many layers of defense against
hacks and exploits. You can prevent Fossil from entering the chroot
jail using the <tt>--nojail</tt> option to the 
[fossil server command](/help?cmd=server)
but you cannot make Fossil hold onto root privileges.  Fossil always drops
root privilege before accepting inputs, for security.


[bld]: https://fossil-scm.org/home/doc/trunk/www/build.wiki
[cj]:  https://en.wikipedia.org/wiki/Chroot
[fls]: ./loadmgmt.md
[mnl]: https://fossil-scm.org/forum/forumpost/90caff30cb
[srv]: ./server/
[obsd]: ./server/openbsd/fastcgi.md#chroot
