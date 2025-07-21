# Serving via systemd on Debian and Ubuntu

[`systemd`][sdhome] is the service management framework in all major
in-support versions of Linux.  There are multiple ways to run Fossil
under `systemd`.

[sdhome]: https://www.freedesktop.org/wiki/Software/systemd/
[wpa]:    https://en.wikipedia.org/wiki/Systemd#Adoption


## Containerized Service

Two of the methods for running [containerized Fossil][cntdoc] integrate
with `systemd`, potentially obviating the more direct methods below:

*   If you take [the Podman method][podman] of running containerized
    Fossil, it opens the `podman generate systemd` option for you, as
    exemplified in [the `fslsrv` script][fslsrv] used on this author’s
    public Fossil-based web site. That script pulls its container images
    from [my Docker Hub repo][dhrepo] to avoid the need for my public
    Fossil server to have build tools and a copy of the Fossil source
    tree. You’re welcome to use my images as-is, or you may use these
    tools to bounce custom builds up through a separate container image
    repo you manage.

*   If you’re willing to give up [a lot of features][nsweak] relative to
    Podman, and you’re willing to tolerate a lot more manual
    administrivia, [the nspawn method][nspawn] has a lot less overhead,
    being a direct feature of `systemd` itself.

Both of these options provide [better security][cntsec] than running
Fossil directly under `systemd`, among [other benefits][cntdoc].

[cntdoc]: ../../containers.md
[cntsec]: ../../containers.md#security
[dhrepo]: https://hub.docker.com/r/tangentsoft/fossil
[fslsrv]: https://tangentsoft.com/fossil/dir?name=bin
[nspawn]: ../../containers.md#nspawn
[nsweak]: ../../containers.md#nspawn-weaknesses
[podman]: ../../containers.md#podman


## User Service

A fun thing you can easily do with `systemd` that you can’t directly do
with older technologies like `inetd` and `xinetd` is to set a server up
as a “user” service.

You can’t listen on TCP port 80 with this method due to security
restrictions on TCP ports in every OS where `systemd` runs, but you can
create a listener socket on a high-numbered (&ge; 1024) TCP port,
suitable for sharing a Fossil repo to a workgroup on a private LAN.

To do this, write the following in
`~/.local/share/systemd/user/fossil.service`:

> ```dosini
[Unit]
Description=Fossil user server
After=network-online.target

[Service]
WorkingDirectory=/home/fossil/museum
ExecStart=/home/fossil/bin/fossil server --port 9000 repo.fossil
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Unlike with `inetd` and `xinetd`, we don’t need to tell `systemd` which
user and group to run this service as, because we’ve installed it
under the account we’re logged into, which `systemd` will use as the
service’s owner.

The result is essentially [the standalone server method](../any/none.md)
coupled with an intelligent service manager that will start it
automatically in the background on system boot, perform automatic
service restarts with back-off logic, and more, making this much more
robust than the by-hand launches of `fossil` in the platform-independent
Fossil server instructions.  The service will stay up until we
explicitly tell it to shut down.

This scheme couples well with [the generic SCGI instructions][scgi] as
it requires a way to run the underlying repository server in the
background. Given that its service port is then proxied by SCGI, it
follows that it doesn’t need to run as a system service. A user service
works perfectly well for this.

Because we’ve set this up as a user service, the commands you give to
manipulate the service vary somewhat from the sort you’re more likely to
find online:

    $ systemctl --user daemon-reload
    $ systemctl --user enable fossil
    $ systemctl --user start fossil
    $ systemctl --user status fossil -l
    $ systemctl --user stop fossil

That is, we don’t need to talk to `systemd` with `sudo` privileges, but
we do need to tell it to look at the user configuration rather than the
system-level configuration.

This scheme isolates the permissions needed by the Fossil server, which
reduces the amount of damage it can do if there is ever a
remotely-triggerable security flaw found in Fossil.

On some `systemd` based OSes, user services only run while that user is
logged in interactively. This is common on systems aiming to provide
desktop environments, where this is the behavior you often want. To
allow background services to continue to run after logout, say:

    $ sudo loginctl enable-linger $USER

You can paste the command just like that into your terminal, since
`$USER` will expand to your login name.

[scgi]: ../any/scgi.md



### System Service Alternative

There are some common reasons that you’d have good cause to install
Fossil as a system-level service rather than the prior user-level one:

*   You’re using [the new `fossil server --cert` feature][sslsrv] to get
    TLS service and want it to listen directly on port 443, rather than be
    proxied, as one had to do before Fossil got the ability to act as a
    TLS server itself.  That requires root privileges, so you can’t run
    it as a user-level service.

*   You’re proxying Fossil with [nginx](./nginx.md) or similar, allowing
    it to bind to high-numbered ports, but because it starts as a system
    service, you can’t get Fossil into the same dependency chain to
    ensure things start up and shut down in the proper order unless it
    *also* runs as a system service.

*   You want to make use of Fossil’s [chroot jail feature][cjail], which
    requires the server to start as root.

There are just a small set of changes required:

1.  Install the unit file to one of the persistent system-level unit
    file directories. Typically, these are:

        /etc/systemd/system
        /lib/systemd/system

2.  Add `User` and `Group` directives to the `[Service]` section so
    Fossil runs as a normal user, preferably one with access only to
    the Fossil repo files, rather than running as `root`.

[sslsrv]: ../../ssl-server.md
[cjail]:  ../../chroot.md


## Socket Activation

Another useful method to serve a Fossil repo via `systemd` is via a
socket listener, which `systemd` calls “[socket activation][sa],”
roughly equivalent to [the ancient `inetd` method](../any/inetd.md).
It’s more complicated, but it has some nice properties.

We first need to define the privileged socket listener by writing
`/etc/systemd/system/fossil.socket`:

> ```dosini
[Unit]
Description=Fossil socket

[Socket]
Accept=yes
ListenStream=80
NoDelay=true

[Install]
WantedBy=sockets.target
```

Note the change of configuration directory from the `~/.local` directory
to the system level. We need to start this socket listener at the root
level because of the low-numbered TCP port restriction we brought up
above.

This configuration says more or less the same thing as the socket part
of an `inetd` entry [exemplified elsewhere in this
documentation](../any/inetd.md).

Next, create the service definition file in that same directory as
`fossil@.service`:

> ```dosini
[Unit]
Description=Fossil socket server
After=network-online.target

[Service]
WorkingDirectory=/home/fossil/museum
ExecStart=/home/fossil/bin/fossil http repo.fossil
StandardInput=socket

[Install]
WantedBy=multi-user.target
```

Notice that we haven’t told `systemd` which user and group to run Fossil
under. Since this is a system-level service definition, that means it
will run as root, which then causes Fossil to [automatically drop into a
`chroot(2)` jail](../../chroot.md) rooted at the `WorkingDirectory`
we’ve configured above, shortly after each `fossil http` call starts.

The `Restart*` directives we had in the user service configuration above
are unnecessary for this method, since Fossil isn’t supposed to remain
running under it. Each HTTP hit starts one Fossil instance, which
handles that single client’s request and then immediately shuts down.

Next, you need to tell `systemd` to reload its system-level
configuration files and enable the listening socket:

    $ sudo systemctl daemon-reload
    $ sudo systemctl enable fossil.socket

And now you can manipulate the socket listener:

    $ sudo systemctl start fossil.socket
    $ sudo systemctl status -l fossil.socket
    $ sudo systemctl stop fossil.socket

Notice that we’re working with the *socket*, not the *service*. The fact
that we’ve given them the same base name and marked the service as an
instantiated service with the “`@`” notation allows `systemd` to
automatically start an instance of the service each time a hit comes in
on the socket that `systemd` is monitoring on Fossil’s behalf. To see
this service instantiation at work, visit a long-running Fossil page
(e.g. `/tarball`) and then give a command like this:

    $ sudo systemctl --full | grep fossil

This will show information about the `fossil` socket and service
instances, which should show your `/tarball` hit handler, if it’s still
running:

    fossil@20-127.0.0.1:80-127.0.0.1:38304.service

You can feed that service instance description to a `systemctl kill`
command to stop that single instance without restarting the whole
`fossil` service, for example.

In all of this, realize that we’re able to manipulate a single socket
listener or single service instance at a time, rather than reload the
whole externally-facing network configuration as with the far more
primitive `inetd` service.

[sa]: http://0pointer.de/blog/projects/socket-activation.html


*[Return to the top-level Fossil server article.](../)*
