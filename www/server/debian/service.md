# Serving via systemd on Debian and Ubuntu

[`systemd`][sdhome] is the default service management framework on
Debian [since version 8][wpa] and Ubuntu since version 15.04, both
released in April 2015.

There are multiple ways to get a service to launch under `systemd`.
We’re going to show two methods which correspond approximately to two of
our generic Fossil server setup methods, the [`inetd`](../any/inetd.md)
and [standalone HTTP server](../any/none.md) methods.

[sdhome]: https://www.freedesktop.org/wiki/Software/systemd/
[wpa]:    https://en.wikipedia.org/wiki/Systemd#Adoption



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

```dosini
    [Unit]
    Description=Fossil user server
    After=network.target

    [Service]
    WorkingDirectory=/home/fossil/museum
    ExecStart=/home/fossil/bin/fossil server --port 9000 repo.fossil
    Restart=always
    RestartSec=3

    [Install]
    WantedBy=sockets.target
    WantedBy=multi-user.target
```

Unlike with `inetd` and `xinetd`, we don’t need to tell `systemd` which
user and group to run this service as, because we’ve installed it as a
user service under the account we’re logged into.

We’ve told `systemd` that we want automatic service restarts with
back-off logic, making this much more robust than the by-hand launches
of `fossil` in the platform-independent Fossil server instructions.  The
service will stay up until we explicitly tell it to shut down.

Because we’ve set this up as a user service, the commands you give to
manipulate the service vary somewhat from the sort you’re more likely to
find online:

        $ systemctl --user daemon-reload
        $ systemctl --user enable fossil
        $ systemctl --user start fossil
        $ systemctl --user status -l fossil
        $ systemctl --user stop fossil

That is, we don’t need to talk to `systemd` with `sudo` privileges, but
we do need to tell it to look at the user configuration rather than the
system-level configuration.

This scheme isolates the permissions needed by the Fossil server, which
reduces the amount of damage it can do if there is ever a
remotely-triggerable security flaw found in Fossil.

A simple and useful modification to the above scheme is to add the
`--scgi` and `--localhost` flags to the `ExecStart` line to replace the
use of `fslsrv` in [the generic SCGI instructions](../any/scgi.md),
giving a much more robust configuration.


## Socket Activation

Another useful method to serve a Fossil repo via `systemd` is via a
“socket listener,” though `systemd` calls it “[socket activation][sa]”.
It’s more complicated, but it has some nice properties.  It is the
feature that allows `systemd` to replace `inetd`, `xinetd`, Upstart, and
several other competing technologies.

We first need to define the privileged socket listener by writing
`/etc/systemd/system/fossil.socket`:

```dosini
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
of an `inted` entry [exemplified elsewhere in this
documentation](../any/inetd.md).

Next, create the service definition file in that same directory as
`fossil@.service`:

```dosini
    [Unit]
    Description=Fossil socket server
    After=network.target

    [Service]
    WorkingDirectory=/home/fossil/museum
    ExecStart=/home/fossil/bin/fossil http repo.fossil
    StandardInput=socket

    [Install]
    WantedBy=sockets.target
    WantedBy=multi-user.target
```

The name change, adding the `@` tells `systemd` this is an “instantiated
service”, meaning that it will create a separate copy of the service
each time it’s called upon. We’ll show the effect of this below.

Notice that we haven’t told `systemd` which user and group to run Fossil
under. Since this is a system-level service definition, that means it
will run as root, which then causes Fossil to [automatically drop into a
`chroot(2)` jail](../../chroot.md) rooted at the `WorkingDirectory`
we’ve configured above.

The `Restart*` directives we had in the user service configuration above
are unnecessary, since Fossil isn’t supposed to remain running under
this configuration. Each HTTP hit starts one Fossil instance, which
handles the client’s request and then immediately shuts down.

Next, you need to tell `systemd` to reload its configuration files and
enable the listening socket:

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
