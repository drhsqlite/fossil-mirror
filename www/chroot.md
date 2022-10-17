# The Server Chroot Jail

If you run Fossil as root in any mode that [serves data on the
network][srv], and you're running it on Unix or a compatible OS, Fossil
will drop itself into a [`chroot(2)` jail][cj] shortly after starting
up, once it's done everything that requires root access. Most commonly,
you run Fossil as root to allow it to bind to TCP port 80 for HTTP
service, since normal users are restricted to ports 1024 and up on OSes
where this behavior occurs.

Fossil uses the owner of the Fossil repository file as its new user
ID when dropping root privileges.

When this happens, Fossil needs to have all of its dependencies inside
the chroot jail in order to continue work.  There are several things you
typically need in order to make things work properly:

*   the repository file(s)

*   `/dev/null` — create it with `mknod(8)` inside the jail directory
    ([Linux example][mnl], [OpenBSD example][obsd])

*   `/dev/urandom` — ditto

*   `/proc` — you might need to mount this virtual filesystem inside the
    jail on Linux systems that make use of [Fossil’s server load
    shedding feature][fls]

*   any shared libraries your `fossil` binary is linked to, unless you
    [configured Fossil with `--static`][bld] to avoid it

Fossil does all of this in order to protect the host OS. You can make it
bypass the jail part of this by passing <tt>--nojail</tt> to <tt>fossil server</tt>,
but you cannot make it skip the dropping of root privileges, on purpose.


[bld]: https://fossil-scm.org/home/doc/trunk/www/build.wiki
[cj]:  https://en.wikipedia.org/wiki/Chroot
[fls]: ./loadmgmt.md
[mnl]: https://fossil-scm.org/forum/forumpost/90caff30cb
[srv]: ./server/
[obsd]: ./server/openbsd/fastcgi.md#chroot
