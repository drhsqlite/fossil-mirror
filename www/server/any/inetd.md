# Serving via inetd

A Fossil server can be launched on-demand by `inetd` by  using the
[`fossil http`](/help/http) command. To do so, add a line like the
following to its configuration file, typically `/etc/inetd.conf`:

    80 stream tcp nowait.1000 root /usr/bin/fossil /usr/bin/fossil http /home/fossil/repo.fossil

In this example, you are telling `inetd` that when an incoming
connection appears on TCP port 80 that it should launch the program
`/usr/bin/fossil` with the arguments shown.  Obviously you will need to
modify the pathnames for your particular setup.  The final argument is
either the name of the fossil repository to be served or a directory
containing multiple repositories.

If you use a non-standard TCP port on systems where the port
specification must be a symbolic name and cannot be numeric, add the
desired name and port to `/etc/services`.  For example, if you want your
Fossil server running on TCP port 12345 instead of 80, you will need to
add:

    fossil          12345/tcp  # fossil server

and use the symbolic name “`fossil`” instead of the numeric TCP port
number (“12345” in the above example) in `inetd.conf`.

Notice that we configured `inetd` to launch Fossil as root. See the
top-level section on “[The Fossil Chroot
Jail](../../chroot.md)” for the consequences of this and
alternatives to it.

You can instead configure `inetd` to bind to a higher-numbered TCP port,
allowing Fossil to be run as a normal user. In that case, Fossil will
not put itself into a chroot jail, because it assumes you have set up
file permissions and such on the server appropriate for that user.

The `inetd` daemon must be enabled for this to work, and it must be
restarted whenever its configuration file changes.

This is a more complicated method than the [standalone HTTP server
method](./none.md), but it has the advantage of only using system
resources when an actual connection is attempted.  If no one ever
connects to that port, a Fossil server will not (automatically) run. It
has the disadvantage of requiring "root" access, which may not be
available to you, either due to local IT policy or because of
restrictions at your shared Internet hosting service.

For further details, see the relevant section in your system's
documentation. The FreeBSD Handbook covers `inetd` in [this
chapter](https://www.freebsd.org/doc/en/books/handbook/network-inetd.html).

*[Return to the top-level Fossil server article.](../)*
