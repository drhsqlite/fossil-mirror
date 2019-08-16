# Serving via SCGI

There is an alternative to running Fossil as a [standalone HTTP
server](./none.md), which is to run it in SimpleCGI (a.k.a. SCGI) mode,
which uses the same [`fossil server`](/help/server) command as for HTTP
service. Simply add the `--scgi` command-line option and the stand-alone
server will speak the SCGI protocol rather than raw HTTP.

This can be used with a web server such as [nginx](http://nginx.org)
which does not support [Fossil’s CGI mode](./cgi.md).

A basic nginx configuration to support SCGI with Fossil looks like this:

        location /example/ {
            include scgi_params;
            scgi_pass localhost:9000;
            scgi_param SCRIPT_NAME "/example";
            scgi_param HTTPS "on";
        }

Start Fossil so that it will respond to nginx’s SCGI calls like this:

        fossil server /path/to/repo.fossil --scgi --localhost --port 9000

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

Fossil requires the `SCRIPT_NAME` environment variable in order to
function properly, but nginx does not provide this variable by default,
so it is necessary to provide it in the configuration.  Failure to do
this will cause Fossil to return an error.

The [example `fslsrv` script](/file/tools/fslsrv) shows off these same
concepts in a more complicated setting. You might want to mine that
script for ideas.

You might want to next read one of the platform-specific versions of this
document, which goes into more detail:

*   [Debian/Ubuntu](../debian/nginx.md)

There is a [separate article](../../tls-nginx.md) showing how to add TLS
encryption to this basic SCGI + nginx setup.
