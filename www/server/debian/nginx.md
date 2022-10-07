# Serving via nginx on Debian and Ubuntu

This document is an extension of [the platform-independent SCGI
instructions][scgii], which may suffice for your purposes if your needs
are simple.

Here, we add more detailed information on nginx itself, plus details
about running it on Debian type OSes. We focus on Debian 10 (Buster) and
Ubuntu 20.04 here, which are common Tier 1 OS offerings for [virtual
private servers][vps] at the time of writing.  This material may not work for older OSes. It is
known in particular to not work as given for Debian 9 and older!

We also cover adding TLS to the basic configuration, because several
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

      server {
          server_name .example.com .example.net "";
          include local/generic;

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
                  expires 7d;
                  add_header Vary Accept-Encoding;
                  access_log off;
              }
          }

          # Redirect everything else to the Fossil instance
          location /code {
              include scgi_params;
              scgi_param SCRIPT_NAME "/code";
              scgi_pass 127.0.0.1:12345;
          }
      }

As you can see, this is a pure extension of [the basic nginx service
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

That teaches `fail2ban` how to recognize the errors logged by Fossil
[as of 2.14](/info/39d7eb0e22). (Earlier versions of Fossil returned
HTTP status code 200 for this, so you couldn’t distinguish a successful
login from a failure.)

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

One of the [many ways](../../ssl.wiki) to provide TLS-encrypted HTTP access
(a.k.a. HTTPS) to Fossil is to run it behind a web proxy that supports
TLS. One such option is nginx on Debian, so we show the details of that
here.

You can extend this guide to other operating systems by following the
instructions found via [the front Certbot web page][cb] instead, telling
it what OS and web stack you’re using. Chances are good that they’ve got
a good guide for you already.


### <a id="leew"></a> Configuring Let’s Encrypt, the Easy Way

If your web serving needs are simple, [Certbot][cb] can configure nginx
for you and keep its certificates up to date. Simply follow Certbot’s
[nginx on Ubuntu 20.04 LTS guide][cbnu].

Unfortunately, the setup above was beyond Certbot’s ability to cope the
last time we tried it. The use of per-subdomain files in particular
confused Certbot, so we had to [arrange these details manually](#lehw),
else the Let’s Encrypt [ACME] exchange failed in the necessary domain
validation steps.

At this point, if your configuration needs are simple, needing only a
single Internet domain and a single Fossil repo, you might wish to try
to reduce the above configuration to a more typical single-file nginx
config, which Certbot might then cope with out of the box.



### <a id="lehw"></a> Configuring Let’s Encrypt, the Hard Way

The primary motivation for this section is that it documents the manual
Certbot configuration on my public Fossil-based site.  I’m addressing
the “me” years hence who needs to upgrade to Ubuntu 22.04 or 24.04 LTS
and has forgotten all of this stuff. 😉


#### Step 1: Shifting into Manual

The first thing we’ll do is install Certbot in the normal way, but we’ll
turn off all of the Certbot automation and won’t follow through with use
of the `--nginx` plugin:

      $ sudo snap install --classic certbot
      $ sudo systemctl disable certbot.timer

Next, edit `/etc/letsencrypt/renewal/example.com.conf` to disable the
nginx plugins. You’re looking for two lines setting the “install” and
“auth” plugins to “nginx”.  You can comment them out or remove them
entirely.


#### Step 2: Configuring nginx

This is a straightforward extension to the HTTP-only configuration
[above](#config):

      server {
          server_name .foo.net;

          include local/tls-common;

          charset utf-8;

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
      server {
          server_name .foo.net;
          root /var/www/foo.net;
          include local/http-certbot-only;
          access_log /var/log/nginx/foo.net-http-access.log;
           error_log /var/log/nginx/foo.net-http-error.log;
      }

One big difference between this and the HTTP-only case is
that we need two `server { }` blocks: one for HTTPS service, and
one for HTTP-only service.


##### HTTP over TLS (HTTPS) Service

The first `server { }` block includes this file, `local/tls-common`:

      listen 443 ssl;

      ssl_certificate     /etc/letsencrypt/live/example.com/fullchain.pem;
      ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

      ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

      ssl_stapling on;
      ssl_stapling_verify on;

      ssl_protocols TLSv1 TLSv1.1 TLSv1.2 TLSv1.3;
      ssl_ciphers "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-CBC-SHA:ECDHE-ECDSA-AES256-CBC-SHA:ECDHE-ECDSA-AES128-CBC-SHA256:ECDHE-ECDSA-AES256-CBC-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-CBC-SHA:ECDHE-RSA-AES256-CBC-SHA:ECDHE-RSA-AES128-CBC-SHA256:ECDHE-RSA-AES256-CBC-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES128-CBC-SHA:DHE-RSA-AES256-CBC-SHA:DHE-RSA-AES128-CBC-SHA256:DHE-RSA-AES256-CBC-SHA256";
      ssl_session_cache shared:le_nginx_SSL:1m;
      ssl_prefer_server_ciphers on;
      ssl_session_timeout 1440m;

These are the common TLS configuration parameters used by all domains
hosted by this server.

The first line tells nginx to accept TLS-encrypted HTTP connections on
the standard HTTPS port. It is the same as `listen 443; ssl on;` in
older versions of nginx.

Since all of those domains share a single TLS certificate, we reference
the same `example.com/*.pem` files written out by Certbot with the
`ssl_certificate*` lines.

The `ssl_dhparam` directive isn’t strictly required, but without it, the
server becomes vulnerable to the [Logjam attack][lja] because some of
the cryptography steps are precomputed, making the attacker’s job much
easier. The parameter file this directive references should be
generated automatically by the Let’s Encrypt package upon installation,
making those parameters unique to your server and thus unguessable. If
the file doesn’t exist on your system, you can create it manually, so:

      $ sudo openssl dhparam -out /etc/letsencrypt/dhparams.pem 2048

Beware, this can take a long time. On a shared Linux host I tried it on
running OpenSSL 1.1.0g, it took about 21 seconds, but on a fast, idle
iMac running LibreSSL 2.6.5, it took 8 minutes and 4 seconds!

The next section is also optional. It enables [OCSP stapling][ocsp], a
protocol that improves the speed and security of the TLS connection
negotiation.

The next section containing the `ssl_protocols` and `ssl_ciphers` lines
restricts the TLS implementation to only those protocols and ciphers
that are currently believed to be safe and secure.  This section is the
one most prone to bit-rot: as new attacks on TLS and its associated
technologies are discovered, this configuration is likely to need to
change. Even if we fully succeed in keeping this document up-to-date in
the face of the evolving security landscape, we’re recommending static
configurations for your server: it will thus be up to you to track
changes in this document and others to merge the changes into your local
static configuration.

Running a TLS certificate checker against your site occasionally is a
good idea. The most thorough service I’m aware of is the [Qualys SSL
Labs Test][qslt], which gives the site I’m basing this guide on an “A+”
rating at the time of this writing. The long `ssl_ciphers` line above is
based on [their advice][qslc]: the default nginx configuration tells
OpenSSL to use whatever ciphersuites it considers “high security,” but
some of those have come to be considered “weak” in the time between that
judgement and the time of this writing. By explicitly giving the list of
ciphersuites we want OpenSSL to use within nginx, we can remove those
that become considered weak in the future.

<a id=”hsts”></a>There are a few things you can do to get an even better
grade, such as to enable [HSTS][hsts]:

      add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;

This prevents a particular variety of [man in the middle attack][mitm]
where our HTTP-to-HTTPS permanent redirect is intercepted, allowing the
attacker to prevent the automatic upgrade of the connection to a secure
TLS-encrypted one.  I didn’t enable that in the configuration above
because it is something a site administrator should enable only after
the configuration is tested and stable, and then only after due
consideration. There are ways to lock your users out of your site by
jumping to HSTS hastily. When you’re ready, there are [guides you can
follow][nest] elsewhere online.


##### HTTP-Only Service

While we’d prefer not to offer HTTP service at all, we need to do so for
two reasons:

*   The temporary reason is that until we get Let’s Encrypt certificates
    minted and configured properly, we can’t use HTTPS yet at all.

*   The ongoing reason is that the Certbot [ACME][acme] HTTP-01
    challenge used by the Let’s Encrypt service only runs over HTTP. This is
    not only because it has to work before HTTPS is first configured,
    but also because it might need to work after a certificate is
    accidentally allowed to lapse to get that server back into a state
    where it can speak HTTPS safely again.

So, from the second `service { }` block, we include this file to set up
the minimal HTTP service we require, `local/http-certbot-only`:

      listen 80;
      listen [::]:80;

      # This is expressed as a rewrite rule instead of an "if" because
      # http://wiki.nginx.org/IfIsEvil
      #rewrite ^(/.well-known/acme-challenge/.*) $1 break;

      # Force everything else to HTTPS with a permanent redirect.
      #return 301 https://$host$request_uri;

As written above, this configuration does nothing other than to tell
nginx that it’s allowed to serve content via HTTP on port 80 as well.
We’ll uncomment the `rewrite` and `return` directives below, when we’re
ready to begin testing.

Notice that most of the nginx directives given [above](#config) moved up
into the TLS `server { }` block, because we eventually want this site to
be as close to HTTPS-only as we can get it.


#### Step 3: Dry Run

We want to first request a dry run, because Let’s Encrypt puts some
rather low limits on how often you’re allowed to request an actual
certificate.  You want to be sure everything’s working before you do
that.  You’ll run a command something like this:

      $ sudo certbot certonly --webroot --dry-run \
         --webroot-path /var/www/example.com \
             -d example.com -d www.example.com \
             -d example.net -d www.example.net \
         --webroot-path /var/www/foo.net \
             -d foo.net -d www.foo.net

There are two key options here.

First, we’re telling Certbot to use its `--webroot` plugin instead of
the automated `--nginx` plugin. With this plugin, Certbot writes the
[ACME][acme] HTTP-01 challenge files to the static web document root
directory behind each domain.  For this example, we’ve got two web
roots, one of which holds documents for two different second-level
domains (`example.com` and `example.net`) with `www` at the third level
being optional.  This is a common sort of configuration these days, but
you needn’t feel that you must slavishly imitate it. The other web root
is for an entirely different domain, also with `www` being optional.
Since all of these domains are served by a single nginx instance, we
need to give all of this in a single command, because we want to mint a
single certificate that authenticates all of these domains.

The second key option is `--dry-run`, which tells Certbot not to do
anything permanent.  We’re just seeing if everything works as expected,
at this point.


##### Troubleshooting the Dry Run

If that didn’t work, try creating a manual test:

      $ mkdir -p /var/www/example.com/.well-known/acme-challenge
      $ echo hi > /var/www/example.com/.well-known/acme-challenge/test

Then try to pull that file over HTTP — not HTTPS! — as
`http://example.com/.well-known/acme-challenge/test`. I’ve found that
using Firefox or Safari is better for this sort of thing than Chrome,
because Chrome is more aggressive about automatically forwarding URLs to
HTTPS even if you requested “`http`”.

In extremis, you can do the test manually:

      $ curl -i http://example.com/.well-known/acme-challenge/test
      HTTP/1.1 200 OK
      Server: nginx/1.14.0 (Ubuntu)
      Date: Sat, 19 Jan 2019 19:43:58 GMT
      Content-Type: application/octet-stream
      Content-Length: 3
      Last-Modified: Sat, 19 Jan 2019 18:21:54 GMT
      Connection: keep-alive
      ETag: "5c436ac2-4"
      Accept-Ranges: bytes

      hi

The key bits you’re looking for here are the “200 OK” response code at
the start and the “hi” line at the end. (Or whatever you wrote in to the
test file.)

If you get a 301 redirect to an `https://` URI, you either haven’t
uncommented the `rewrite` line for HTTP-only service for this directory,
or there’s some other problem with the “redirect to HTTPS” config.

If you get a 404 or other error response, you need to look into your web
server logs to find out what’s going wrong.

If you’re still running into trouble, the log file written by Certbot
can be helpful.  It tells you where it’s writing the ACME files early in
each run.



#### Step 4: Getting Your First Certificate

Once the dry run is working, you can drop the `--dry-run` option and
re-run the long command above.  (The one with all the `--webroot*`
flags.) This should now succeed, and it will save all of those flag
values to your Let’s Encrypt configuration file, so you don’t need to
keep giving them.



#### Step 5: Test It

Edit the `local/http-certbot-only` file and uncomment the `redirect` and
`return` directives, then restart your nginx server and make sure it now
forces everything to HTTPS like it should:

      $ sudo systemctl restart nginx

Test ideas:

*   Visit both Fossil and non-Fossil URLs

*   Log into the repo, log out, and log back in

*   Clone via `http`: ensure that it redirects to `https`, and that
    subsequent `fossil sync` commands go directly to `https` due to the
    301 permanent redirect.

This forced redirect is why we don’t need the Fossil Admin &rarr; Access
"Redirect to HTTPS on the Login page" setting to be enabled.  Not only
is it unnecessary with this HTTPS redirect at the front-end proxy level,
it would actually [cause an infinite redirect loop if
enabled](./ssl.wiki#rloop).



#### Step 6: Switch to HTTPS Sync

Fossil remembers permanent HTTP-to-HTTPS redirects on sync since version
2.9, so all you need to do to switch your syncs to HTTPS is:

      $ fossil sync -R /path/to/repo.fossil
    

#### Step 7: Renewing Automatically

Now that the configuration is solid, you can renew the LE cert with the
`certbot` command from above without the `--dry-run` flag plus a restart
of nginx:

      sudo certbot certonly --webroot \
         --webroot-path /var/www/example.com \
             -d example.com -d www.example.com \
             -d example.net -d www.example.net \
         --webroot-path /var/www/foo.net \
             -d foo.net -d www.foo.net
      sudo systemctl restart nginx

I put those commands in a script in the `PATH`, then arrange to call that
periodically.  Let’s Encrypt doesn’t let you renew the certificate very
often unless forced, and when forced there’s a maximum renewal counter.
Nevertheless, some people recommend running this daily and just letting
it fail until the server lets you renew.  Others arrange to run it no
more often than it’s known to work without complaint.  Suit yourself.


[acme]: https://en.wikipedia.org/wiki/Automated_Certificate_Management_Environment
[cb]:   https://certbot.eff.org/
[cbnu]: https://certbot.eff.org/lets-encrypt/ubuntufocal-nginx
[hsts]: https://en.wikipedia.org/wiki/HTTP_Strict_Transport_Security
[lja]:  https://en.wikipedia.org/wiki/Logjam_(computer_security)
[mitm]: https://en.wikipedia.org/wiki/Man-in-the-middle_attack
[nest]: https://www.nginx.com/blog/http-strict-transport-security-hsts-and-nginx/
[ocsp]: https://en.wikipedia.org/wiki/OCSP_stapling
[qslc]: https://github.com/ssllabs/research/wiki/SSL-and-TLS-Deployment-Best-Practices
[qslt]: https://www.ssllabs.com/ssltest/

*[Return to the top-level Fossil server article.](../)*
