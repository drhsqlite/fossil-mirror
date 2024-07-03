# Serving via nginx on Debian and Ubuntu

This document is an extension of [the platform-independent SCGI
instructions][scgii], which may suffice for your purposes if your needs
are simple.

Here, we add more detailed information on nginx itself, plus details
about running it on Debian type OSes. This document was originally
written for and tested on Debian 10 (Buster) and Ubuntu 20.04, which
were common Tier 1 OS offerings for [virtual private servers][vps]
at the time. The same configuration appears to run on Ubuntu 22.04
LTS without change. This material may not work for older OSes. It is
known in particular to not work as given for Debian 9 and older!

We also cover [adding TLS](#tls) to the basic configuration, because several
details depend on the host OS and web stack details. Besides, TLS is
widely considered part of the baseline configuration these days.

[scgii]: ../any/scgi.md
[vps]:   https://en.wikipedia.org/wiki/Virtual_private_server


## <a id="benefits"></a>Benefits

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

    *   <p>One is entirely static, not involving any dynamic content or
        Fossil integration at all.</p>

    *   <p>Another is served almost entirely by Fossil, with a few select
        static content exceptions punched past Fossil, which are handled
        entirely via nginx.</p>

    *   <p>The other two domains are aliases for one another — e.g.
        `example.com` and `example.net` — with most of the content being
        static.  This pair of domains has several unrelated Fossil repo
        proxies attached to various sections of the URI hierarchy.</p>

    By using nginx, I was able to do all of the above with minimal
    repetition between the site configurations.

*   **Integration** — Because nginx is so popular, it integrates with
many different technologies, and many other systems integrate with it in
turn.  This makes it great middleware, sitting between the outer web
world and interior site services like Fossil. It allows Fossil to
participate seamlessly as part of a larger web stack.

*   **Availability** — nginx is already in most operating system binary
package repositories, so you don’t need to go out of your way to get it.


## <a id="modes"></a>Fossil Service Modes

Fossil provides four major ways to access a repository it’s serving
remotely, three of which are straightforward to use with nginx:

*   **HTTP** — Fossil has [a built-in HTTP server](../any/none.md).
    While this method is efficient and it’s
    possible to use nginx to proxy access to another HTTP server, we
    don’t see any particularly good reason to make nginx reinterpret
    Fossil’s own implementation of HTTP when we have a better option.
    (But see [below](#http).)

*   **SSH** — This method exists primarily to avoid the need for HTTPS,
    but we *want* HTTPS. (We’ll get to that [below](#tls).)
    There is probably a way to get nginx to proxy Fossil to HTTPS via
    SSH, but it would be pointlessly complicated.

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

SCGI it is, then.

[scgip]: https://en.wikipedia.org/wiki/Simple_Common_Gateway_Interface


## <a id="deps"></a>Installing the Dependencies

The first step is to install some non-default packages we’ll need. SSH into
your server, then say:

    $ sudo apt install fossil nginx

You can leave “`fossil`” out of that if you’re building Fossil from
source to get a more up-to-date version than is shipped with the host
OS.


## <a id="scgi"></a>Running Fossil in SCGI Mode

For the following nginx configuration to work, it needs to contact a
background Fossil instance speaking the SCGI protocol. There are
[many ways](../) to set that up, such as [with `systemd`](./service.md)
on mainstream Linux distros.  Another way is to [containerize][ctz] your
repository servers, then use the [`fslsrv` wrapper for Podman][fspm] to
generate `systemd` units for use by the front-end proxy.

However you do it, you need to match up the TCP port numbers between it
and those in the nginx configuration below.

[ctz]:  ../../containers.md
[fspm]: https://tangentsoft.com/fossil/dir/bin


## <a id="config"></a>Configuration

On Debian and Ubuntu systems the primary user-level configuration file
for nginx is `/etc/nginx/sites-enabled/default`. I recommend that this
file contain only a list of include statements, one for each site that
server hosts:

    include local/example.com
    include local/foo.net

Those files then each define one domain’s configuration.  Here,
`/etc/nginx/local/example.com` contains the configuration for
`*.example.com` and its alias `*.example.net`; and `local/foo.net`
contains the configuration for `*.foo.net`.

The configuration for our `example.com` web site, stored in
`/etc/nginx/sites-enabled/local/example.com` is:

----

    server {
        server_name .example.com .example.net "";
        include local/generic;
        include local/code;

        access_log /var/log/nginx/example.com-https-access.log;
        error_log /var/log/nginx/example.com-https-error.log;

        # Bypass Fossil for the static documentation generated from
        # our source code by Doxygen, so it merges into the embedded
        # doc URL hierarchy at Fossil’s $ROOT/doc without requiring that
        # these generated files actually be stored in the repo.  This
        # also lets us set aggressive caching on these docs, since
        # they rarely change.
        location /code/doc/html {
            root /var/www/example.com/code/doc/html;

            location ~* \.(html|ico|css|js|gif|jpg|png)$ {
                add_header Vary Accept-Encoding;
                access_log off;
                expires 7d;
            }
        }

        # Redirect everything under /code to the Fossil instance
        location /code {
            include local/code;

            # Extended caching for URLs that include unique IDs
            location ~ "/(artifact|doc|file|raw)/[0-9a-f]{40,64}" {
                add_header Cache-Control "public, max-age=31536000, immutable";
                include local/code;
                access_log off;
            }

            # Lesser caching for URLs likely to be quasi-static
            location ~* \.(css|gif|ico|js|jpg|png)$ {
                add_header Vary Accept-Encoding;
                include local/code;
                access_log off;
                expires 7d;
            }
        }
    }

----

As you can see, this is a pure extension of [the basic nginx service
configuration for SCGI][scgii], showing off a few ideas you might want to
try on your own site, such as static asset proxying.

You also need a `local/code` file containing:

    include scgi_params;
    scgi_pass 127.0.0.1:12345;
    scgi_param SCRIPT_NAME "/code";

We separate that out because nginx refuses to inherit certain settings
between nested location blocks, so rather than repeat them, we extract
them to this separate file and include it from both locations where it’s
needed. You see this above where we set far-future expiration dates on
files served by Fossil via URLs that contain hashes that change when the
content changes. It tells your browser that the content of these URLs
can never change without the URL itself changing, which makes your
Fossil-based site considerably faster.

Similarly, the `local/generic` file referenced above helps us reduce unnecessary
repetition among the multiple sites this configuration hosts:

    root /var/www/$host;

    listen 80;
    listen [::]:80;

    charset utf-8;

There are some configuration directives that nginx refuses to substitute
variables into, citing performance considerations, so there is a limit
to how much repetition you can squeeze out this way. One such example
are the `access_log` and `error_log` directives, which follow an obvious
pattern from one host to the next. Sadly, you must tolerate some
repetition across `server { }` blocks when setting up multiple domains
on a single server.

The configuration for `foo.net` is similar.

See [the nginx docs](https://nginx.org/en/docs/) for more ideas.


## <a id="http"></a>Proxying HTTP Anyway

[Above](#modes), we argued that proxying SCGI is a better option than
making nginx reinterpret Fossil’s own implementation of HTTP.  If you
want Fossil to speak HTTP, just [set Fossil up as a standalone
server](../any/none.md). And if you want nginx to [provide TLS
encryption for Fossil](#tls), proxying HTTP instead of SCGI provides no
benefit.

However, it is still worth showing the proper method of proxying
Fossil’s HTTP server through nginx if only to make reading nginx
documentation on other sites easier:

    location /code {
        rewrite ^/code(/.*) $1 break;
        proxy_pass http://127.0.0.1:12345;
    }

The most common thing people get wrong when hand-rolling a configuration
like this is to get the slashes wrong. Fossil is sensitive to this. For
instance, Fossil will not collapse double slashes down to a single
slash, as some other HTTP servers will.


## <a id="large-uv"></a> Allowing Large Unversioned Files

By default, nginx only accepts HTTP messages [up to a
meg](http://nginx.org/en/docs/http/ngx_http_core_module.html#client_max_body_size)
in size. Fossil chunks its sync protocol such that this is not normally
a problem, but when sending [unversioned content][uv], it uses a single
message for the entire file. Therefore, if you will be storing files
larger than this limit as unversioned content, you need to raise the
limit. Within the `location` block:

    # Allow large unversioned file uploads, such as PDFs
    client_max_body_size 20M;

[uv]: ../../unvers.wiki


## <a id="fail2ban"></a> Integrating `fail2ban`

One of the nice things that falls out of proxying Fossil behind nginx is
that it makes it easier to configure `fail2ban` to recognize attacks on
Fossil and automatically block them. Fossil logs the sorts of errors we
want to detect, but it does so in places like the repository’s admin
log, a SQL table, which `fail2ban` doesn’t know how to query. By putting
Fossil behind an nginx proxy, we convert these failures to log file
form, which `fail2ban` is designed to handle.

First, install `fail2ban`, if you haven’t already:

    sudo apt install fail2ban

We’d like `fail2ban` to react to Fossil `/login` failures.  The stock
configuration of `fail2ban` only detects a few common sorts of SSH
attacks by default, and its included (but disabled) nginx attack
detectors don’t include one that knows how to detect an attack on
Fossil.  We have to teach it by putting the following into
`/etc/fail2ban/filter.d/nginx-fossil-login.conf`:

    [Definition]
    failregex = ^<HOST> - .*POST .*/login HTTP/..." 401

That teaches `fail2ban` how to recognize the errors logged by Fossil.

Then in `/etc/fail2ban/jail.local`, add this section:

    [nginx-fossil-login]
    enabled = true
    logpath = /var/log/nginx/*-https-access.log

The last line is the key: it tells `fail2ban` where we’ve put all of our
per-repo access logs in the nginx config above.

There’s a [lot more you can do][dof2b], but that gets us out of scope of
this guide.


[dof2b]: https://www.digitalocean.com/community/tutorials/how-to-protect-an-nginx-server-with-fail2ban-on-ubuntu-14-04


## <a id="tls"></a> Adding TLS (HTTPS) Support

One of the [many ways](../../ssl.wiki) to provide TLS-encrypted HTTP
access (a.k.a. HTTPS) to Fossil is to run it behind a web proxy that
supports TLS. Because one such option is nginx, it’s best to delegate
TLS to it if you were already using nginx for some other reason, such as
static content serving, with only part of the site being served by
Fossil.

The simplest way by far to do this is to use [Let’s Encrypt][LE]’s
[Certbot][CB], which can configure nginx for you and keep its
certificates up to date. You need but follow their [nginx on Ubuntu 20
guide][CBU]. We had trouble with this in the past, but either Certbot
has gotten smarter or our nginx configurations have gotten simpler, so
we have removed the manual instructions we used to have here.

You may wish to include something like this from each `server { }`
block in your configuration to enable TLS in a common, secure way:

```
# Tell nginx to accept TLS-encrypted HTTPS on the standard TCP port.
listen 443 ssl;
listen [::]:443 ssl;

# Reference the TLS cert files produced by Certbot.
ssl_certificate     /etc/letsencrypt/live/example.com/fullchain.pem;
ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

# Load the Let's Encrypt Diffie-Hellman parameters generated for
# this server.  Without this, the server is vulnerable to Logjam.
ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

# Tighten things down further, per Qualys’ and Certbot’s advice.
ssl_session_cache shared:le_nginx_SSL:1m;
ssl_protocols TLSv1.2 TLSv1.3;
ssl_prefer_server_ciphers on;
ssl_session_timeout 1440m;

# Offer OCSP certificate stapling.
ssl_stapling on;
ssl_stapling_verify on;

# Enable HSTS.
include local/enable-hsts;
```

The [HSTS] step is optional and should be applied only after due
consideration, since it has the potential to lock users out of your
site if you later change your mind on the TLS configuration.
The `local/enable-hsts` file it references is simply:

    add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;

It’s a separate file because nginx requires that headers like this be
applied separately for each `location { }` block. We’ve therefore
factored this out so you can `include` it everywhere you need it.

The [OCSP] step is optional, but recommended.

You may find [Qualys’ SSL Server Test][QSLT] helpful in verifying that
you have set all this up correctly, and that the configuration is
strong. We’ve found their [best practices doc][QSLC] to be helpful.  As
of this writing, the above configuration yields an A+ rating when run on
Ubuntu 22.04.01 LTS.

[CB]:   https://certbot.eff.org/
[CBU]:  https://certbot.eff.org/instructions?ws=nginx&os=ubuntufocal
[LE]:   https://letsencrypt.org/
[HSTS]: https://www.nginx.com/blog/http-strict-transport-security-hsts-and-nginx/
[OCSP]: https://en.wikipedia.org/wiki/OCSP_stapling
[QSLC]: https://github.com/ssllabs/research/wiki/SSL-and-TLS-Deployment-Best-Practices
[QSLT]: https://www.ssllabs.com/ssltest/

<div style="height:50em" id="this-space-intentionally-left-blank"></div>

*[Return to the top-level Fossil server article.](../)*
