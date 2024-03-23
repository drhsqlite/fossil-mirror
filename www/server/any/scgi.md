# Serving via SCGI

There is an alternative to running Fossil as a [standalone HTTP
server](./none.md), which is to run it in SimpleCGI (a.k.a. SCGI) mode,
which uses the same [`fossil server`](/help/server) command as for HTTP
service. Simply add the `--scgi` command-line option and the stand-alone
server will speak the SCGI protocol rather than raw HTTP.

This can be used with a web server such as [nginx](http://nginx.org)
which does not support [Fossil’s CGI mode](./cgi.md).

A basic nginx configuration to support SCGI with Fossil looks like this:

    location /code/ {
        include scgi_params;
        scgi_param SCRIPT_NAME "/code";
        scgi_pass localhost:9000;
    }

The `scgi_params` file comes with nginx, and it simply translates nginx
internal variables to `scgi_param` directives to create SCGI environment
variables for the proxied program; in this case, Fossil. Our explicit
`scgi_param` call to define `SCRIPT_NAME` adds one more variable to this
set, which is necessary for this configuration to work properly, because
our repo isn’t at the root of the URL hierarchy. Without it, when Fossil
generates absolute URLs, they’ll be missing the `/code` part at the
start, which will typically cause [404 errors][404].

The final directive simply tells nginx to proxy all calls to URLs under
`/code` down to an SCGI program on TCP port 9000. We can temporarily
set Fossil up as a server on that port like so:

    $ fossil server /path/to/repo.fossil --scgi --localhost --port 9000 &

The `--scgi` option switches Fossil into SCGI mode from its default,
which is [stand-alone HTTP server mode](./none.md). All of the other
options discussed in that linked document — such as the ability to serve
a directory full of Fossil repositories rather than just a single
repository — work the same way in SCGI mode.

The `--localhost` option is simply good security: we’re using nginx to
expose Fossil service to the outside world, so there is no good reason
to allow outsiders to contact this Fossil SCGI server directly.

Giving an explicit non-default TCP port number via `--port` is a good
idea to avoid conflicts with use of Fossil’s default TCP service port,
8080, which may conflict with local uses of `fossil ui` and such.

We characterized the SCGI service start command above as “temporary”
because running Fossil in the background like that means it won’t start
back up on a reboot of the server. A simple solution to that is to add
that command to `/etc/rc.local` on systems that have it. However, you
might want to consider setting Fossil up as an OS service instead, so
that you get the benefits of the platform’s service management
framework:

*   [Linux (systemd)](../debian/service.md)
*   [Windows service](../windows/service.md)
*   [macOS (launchd)](../macos/service.md)
*   [xinetd](../any/xinetd.md)
*   [inetd](../any/inetd.md)

We go into more detail on nginx service setup with Fossil in our
[Debian/Ubuntu specific guide](../debian/nginx.md), which also
gets you TLS service.

Similarly, our [OpenBSD specific guide](../openbsd/fastcgi.md) details how
to setup a Fossil server using httpd and FastCGI on OpenBSD.

*[Return to the top-level Fossil server article.](../)*

[404]: https://en.wikipedia.org/wiki/HTTP_404
