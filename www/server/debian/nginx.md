# Serving via nginx on Debian

This document is an extension of [the platform-independent SCGI
instructions][scgii], which may suffice for your purposes if your needs
are simple.

Here, we add more detailed information on nginx itself, plus details
about running it on Debian type OSes. We focus on Debian 10 (Buster) and
Ubuntu 18.04 here, which are common Tier 1 OS offerings for [virtual
private servers][vps].  This material may not work for older OSes. It is
known in particular to not work as given for Debian 9 and older!

If you want to add TLS to this configuration, that is covered [in a
separate document][tls] which was written with the assumption that
you’ve read this first.

[scgii]: ../any/scgi.md
[tls]:   ../../tls-nginx.md
[vps]:   https://en.wikipedia.org/wiki/Virtual_private_server


## <a name="benefits"></a>Benefits

This scheme is considerably more complicated than the [standalone HTTP
server](../any/none.md) and [CGI options](../any/cgi.md). Even with the
benefit of this guide and pre-built binary packages, it requires quite a
bit of work to set it up. Why should you put up with this complexity?
Because it gives many benefits that are difficult or impossible to get
with the less complicated options:

*   **Power** — nginx is one of the most powerful web servers in the
    world. The chance that you will run into a web serving wall that you
    can’t scale with nginx is very low.

    To give you some idea of the sort of thing you can readily
    accomplish with nginx, your author runs a single public web server
    that provides transparent name-based virtual hosting for four
    separate domains:

    *   One is entirely static, not involving any dynamic content or
        Fossil integration at all.

    *   Another is served almost entirely by Fossil, with a few select
        static content exceptions punched past Fossil, which are handled
        entirely via nginx.

    *   The other two domains are aliases for one another — e.g.
        `example.com` and `example.net` — with most of the content being
        static.  This pair of domains has three different Fossil repo
        proxies attached to various sections of the URI hierarchy.

    By using nginx, I was able to do all of the above with minimal
    repetition between the site configurations.

*   **Integration** — Because nginx is so popular, it integrates with
many different technologies, and many other systems integrate with it in
turn.  This makes it great middleware, sitting between the outer web
world and interior site services like Fossil. It allows Fossil to
participate seamlessly as part of a larger web stack.

*   **Availability** — nginx is already in most operating system binary
package repositories, so you don’t need to go out of your way to get it.


## <a name="modes"></a>Fossil Service Modes

Fossil provides four major ways to access a repository it’s serving
remotely, three of which are straightforward to use with nginx:

*   **HTTP** — Fossil has a built-in HTTP server: [`fossil
    server`](/help/server).  While this method is efficient and it’s
    possible to use nginx to proxy access to another HTTP server, this
    option is overkill for our purposes.  nginx is itself a fully
    featured HTTP server, so we will choose in this guide not to make
    nginx reinterpret Fossil’s implementation of HTTP.

*   **CGI** — This method is simple but inefficient, because it launches
    a separate Fossil instance on every HTTP hit.

    Since Fossil is a relatively small self-contained program, and it’s
    designed to start up quickly, this method can work well in a
    surprisingly large number of cases.

    Nevertheless, we will avoid this option in this document because
    we’re already buying into a certain amount of complexity here in
    order to gain power.  There’s no sense in throwing away any of that
    hard-won performance on CGI overhead.

*   **SCGI** — The [SCGI protocol][scgip] provides the simplicity of CGI
    without its performance problems.

*   **SSH** — This method exists primarily to avoid the need for HTTPS,
    but we *want* HTTPS. (We’ll get to that in [another document][tls].)
    There is probably a way to get nginx to proxy Fossil to HTTPS via
    SSH, but it would be pointlessly complicated.

SCGI it is, then.

[scgip]: https://en.wikipedia.org/wiki/Simple_Common_Gateway_Interface


## <a name="deps"></a>Installing the Dependencies

The first step is to install some non-default packages we’ll need. SSH into
your server, then say:

       $ sudo apt install fossil nginx


## <a name="scgi"></a>Running Fossil in SCGI Mode

I run my Fossil SCGI server instances with a variant of [the `fslsrv`
shell script](/file/tools/fslsrv) currently hosted in the Fossil source
code repository. You’ll want to download that and make a copy of it, so
you can customize it to your particular needs.

This script allows running multiple Fossil SCGI servers, one per
repository, each bound to a different high-numbered `localhost` port, so
that only nginx can see and proxy them out to the public.  The
“`example`” repo is on TCP port localhost:12345, and the “`foo`” repo is
on localhost:12346.

As written, the `fslsrv` script expects repositories to be stored in the
calling user’s home directory under `~/museum`, because where else do
you keep Fossils?

That home directory also needs to have a directory to hold log files,
`~/log/fossil/*.log`. Fossil doesn’t put out much logging, but when it
does, it’s better to have it captured than to need to re-create the
problem after the fact.

The use of `--baseurl` in this script lets us have each Fossil
repository mounted in a different location in the URL scheme.  Here, for
example, we’re saying that the “`example`” repository is hosted under
the `/code` URI on its domains, but that the “`foo`” repo is hosted at
the top level of its domain.  You’ll want to do something like the
former for a Fossil repo that’s just one piece of a larger site, but the
latter for a repo that is basically the whole point of the site.

You might also want another script to automate the update, build, and
deployment steps for new Fossil versions:

       #!/bin/sh
       cd $HOME/src/fossil/trunk
       fossil up
       make -j11
       killall fossil
       sudo make install
       fslsrv

The `killall fossil` step is needed only on OSes that refuse to let you
replace a running binary on disk.

As written, the `fslsrv` script assumes a Linux environment.  It expects
`/bin/bash` to exist, and it depends on non-POSIX tools like `pgrep`.
It should not be difficult to port to systems like macOS or the BSDs.


## <a name="config"></a>Configuration

On Debian and Ubuntu systems the primary user-level configuration file
for nginx is `/etc/nginx/sites-enabled/default`. I recommend that this
file contain only a list of include statements, one for each site that
server hosts:

      include local/example
      include local/foo

Those files then each define one domain’s configuration.  Here,
`/etc/nginx/local/example` contains the configuration for
`*.example.com` and `*.example.net`; and `local/foo` contains the
configuration for `*.foo.net`.

The configuration for our `foo.net` web site, stored in
`/etc/nginx/sites-enabled/local/foo` is:

      server {
          server_name .foo.net;
          include local/generic;

          access_log /var/log/nginx/foo.net-https-access.log;
           error_log /var/log/nginx/foo.net-https-error.log;

          # Bypass Fossil for the static Doxygen docs
          location /doc/html {
              root /var/www/foo.net;

              location ~* \.(html|ico|css|js|gif|jpg|png)$ {
                  expires 7d;
                  add_header Vary Accept-Encoding;
                  access_log off;
              }
          }

          # Redirect everything else to the Fossil instance
          location / {
              include scgi_params;
              scgi_pass 127.0.0.1:12345;
              scgi_param HTTPS "on";
              scgi_param SCRIPT_NAME "";
          }
      }

As you can see, this is a simple extension of [the basic nginx service
configuration for SCGI][scgii], showing off a few ideas you might want to
try on your own site, such as static asset proxying.

The `local/generic` file referenced above helps us reduce unnecessary
repetition among the multiple sites this configuration hosts:

      root /var/www/$host;

      listen 80;
      listen [::]:80;

      charset utf-8;

There are some configuration directives that nginx refuses to substitute
variables into, citing performance considerations, so there is a limit
to how much repetition you can squeeze out this way. One such example is
the `access_log` and `error_log` directives, which follow an obvious
pattern from one host to the next. Sadly, you must tolerate some
repetition across `server { }` blocks when setting up multiple domains
on a single server.

The configuration for `example.com` and `example.net` is similar.

See [the nginx docs](http://nginx.org/en/docs/) for more ideas.
