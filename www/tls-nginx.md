# Proxying Fossil via HTTPS with nginx

One of the [many ways](./ssl.wiki) to provide TLS-encrypted HTTP access
(a.k.a. HTTPS) to Fossil is to run it behind a web proxy that supports
TLS. This document explains how to use the powerful [nginx web
server](http://nginx.org/) to do that.

This document is an extension of the [Serving via nginx on Debian][nod]
document. Please read that first, then come back here to extend its
configuration with TLS.

[nod]: ./server/debian/nginx.md


## Install Certbot

The [nginx-on-Debian document][nod] had you install a few non-default
packages to the system, but there’s one more you need for this guide:

       $ sudo apt install certbot

You can extend this guide to other operating systems by following the
instructions found via [the front Certbot web page][cb] instead, telling
it what OS and web stack you’re using. Chances are good that they’ve got
a good guide for you already.


# Configuring Let’s Encrypt, the Easy Way

If your web serving needs are simple, [Certbot][cb] can configure nginx
for you and keep its certificates up to date. Simply follow Certbot’s
[nginx on Ubuntu 18.04 LTS guide][cbnu]. We’d recommend one small
change: to use the version of Certbot in the Ubuntu package repository
rather than download it from the Certbot site.

You should be able to use the nginx configuration given in our [Serving
via nginx on Debian][nod] guide with little to no change. The main thing
to watch out for is that the TCP port number in the nginx configuration
needs to match the value you gave when starting Fossil. If you followed
that guide’s advice, it will be 9000.  Another option is to use [the
`fslsrv` script](/file/tools/fslsrv), in which case the TCP port number
will be 12345 or higher.


# Configuring Let’s Encrypt, the Hard Way

If you’re finding that you can’t get certificates to be issued or
renewed using the Easy Way instructions, the problem is usually that
your nginx configuration is too complicated for Certbot’s `--nginx`
plugin to understand. It attempts to rewrite your nginx configuration
files on the fly to achieve the renewal, and if it doesn’t put its
directives in the right locations, the domain verification can fail.

Let’s Encrypt uses the [Automated Certificate Management
Environment][acme] protocol (ACME) to determine whether a given client
actually has control over the domain(s) for which it wants a certificate
minted.  Let’s Encrypt will not blithely let you mint certificates for
`google.com` and `paypal.com` just because you ask for it!

Your author’s configuration, glossed [in the HTTP-only guide][nod],
is complicated enough that
the current version of Certbot (0.28 at the time of this writing) can’t
cope with it.  That’s the primary motivation for me to write this guide:
I’m addressing the “me” years hence who needs to upgrade to Ubuntu 20.04
or 22.04 LTS and has forgotten all of this stuff. 😉


## Step 1: Shifting into Manual

The first thing to do is to turn off all of the Certbot automation,
because it’ll only get in our way.  First, disable the Certbot package’s
automatic background updater:

      $ sudo systemctl disable certbot.timer

Next, edit `/etc/letsencrypt/renewal/example.com.conf` to disable the
nginx plugins. You’re looking for two lines setting the “install” and
“auth” plugins to “nginx”.  You can comment them out or remove them
entirely.


## Step 2: Configuring nginx

This is a straightforward extension to [the HTTP-only
configuration](./server/debian/nginx.md#config):

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


### HTTP over TLS (HTTPS) Service

The first `server { }` block includes this file, `local/tls-common`:

      listen 443 ssl;

      ssl_certificate     /etc/letsencrypt/live/example.com/fullchain.pem;
      ssl_certificate_key /etc/letsencrypt/live/example.com/privkey.pem;

      ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

      ssl_stapling on;
      ssl_stapling_verify on;

      ssl_protocols TLSv1 TLSv1.1 TLSv1.2;
      ssl_ciphers "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES256-SHA256”;
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
change. Even if we fully succeed in [keeping this document
up-to-date](#evolution), the nature of this guide is to recommend static
configurations for your server. You will have to keep an eye on this
sort of thing and evolve your local configuration as the world changes
around it.

Running a TLS certificate checker against your site occasionally is a
good idea. The most thorough service I’m aware of is the [Qualys SSL
Labs Test][qslt], which gives the site I’m basing this guide on an “A”
rating at the time of this writing. The long `ssl_ciphers` line above is
based on [their advice][qslc]: the default nginx configuration tells
OpenSSL to use whatever ciphersuites it considers “high security,” but
some of those have come to be considered “weak” in the time between that
judgement and the time of this writing. By explicitly giving the list of
ciphersuites we want OpenSSL to use within nginx, we can remove those
that become considered weak in the future.

<a id=”hsts”></a>There are a few things you can do to get an even better
grade, such as to enable [HSTS][hsts], which prevents a particular
variety of [man in the middle attack][mitm] where our HTTP-to-HTTPS
permanent redirect is intercepted, allowing the attacker to prevent the
automatic upgrade of the connection to a secure TLS-encrypted one.  I
didn’t enable that in the configuration above, because it is something a
site administrator should enable only after the configuration is tested
and stable, and then only after due consideration. There are ways to
lock your users out of your site by jumping to HSTS hastily. When you’re
ready, there are [guides you can follow][nest] elsewhere online.


### HTTP-Only Service

While we’d prefer not to offer HTTP service at all, we need to do so for
two reasons:

*   The temporary reason is that until we get Let’s Encrypt certificates
    minted and configured properly, we can’t use HTTPS yet at all.

*   The ongoing reason is that the Certbot [ACME][acme] HTTP-01
    challenge used by the Let’s Encrypt service only runs over HTTP. This is
    not only because it has to work before HTTPS is first configured,
    but also because it might need to work after a certificate is
    accidentally allowed to lapse, to get that server back into a state
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

Notice that this configuration is very different from that in the
[HTTP-only nginx on Debian][nod] guide. Most of that guide’s nginx
directives moved up into the TLS `server { }` block, because we
eventually want this site to be as close to HTTPS-only as we can get it.


## Step 3: Dry Run

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
you needn’t feel that you must slavishly imitate it; the other web root
is for an entirely different domain, also with `www` being optional.
Since all of these domains are served by a single nginx instance, we
need to give all of this in a single command, because we want to mint a
single certificate that authenticates all of these domains.

The second key option is `--dry-run`, which tells Certbot not to do
anything permanent.  We’re just seeing if everything works as expected,
at this point.


### Troubleshooting the Dry Run

If that didn’t work, try creating a manual test:

      $ mkdir -p /var/www/example.com/.well-known/acme-challenge
      $ echo hi > /var/www/example.com/.well-known/acme-challenge/test

Then try to pull that file over HTTP — not HTTPS! — as
`http://example.com/.well-known/acme-challenge/test`. I’ve found that
using Firefox or Safari is better for this sort of thing than Chrome,
because Chrome is more aggressive about automatically forwarding URLs to
HTTPS even if you requested “`http`”.

In extremis, you can do the test manually:

      $ telnet foo.net 80
      GET /.well-known/acme-challenge/test HTTP/1.1
      Host: example.com

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

You type the first two lines at the remote system, plus the doubled
“Enter” to create the blank line, and you get something back that
hopefully looks like the rest of the text above.

The key bits you’re looking for here are the “hi” line at the end — the
document content you created above — and the “200 OK” response code. If
you get a 404 or other error response, you need to look into your web
server logs to find out what’s going wrong.

Note that it’s important to do this test with HTTP/1.1 when debugging a
name-based virtual hosting configuration like this. Unless you test only
with the primary domain name alias for the server, this test will fail.
Using the example configuration above, you can only use the
easier-to-type HTTP/1.0 protocol to test the `foo.net` alias.

If you’re still running into trouble, the log file written by Certbot
can be helpful.  It tells you where it’s writing it early in each run.



## Step 4: Getting Your First Certificate

Once the dry run is working, you can drop the `--dry-run` option and
re-run the long command above.  (The one with all the `--webroot*`
flags.) This should now succeed, and it will save all of those flag
values to your Let’s Encrypt configuration file, so you don’t need to
keep giving them.



## Step 5: Test It

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



## Step 6: Re-Point Fossil at Your Repositories

As of Fossil 2.9, the permanent HTTP-to-HTTPS redirect we enabled above
causes Fossil to remember the new URL automatically the first time it’s
redirected to it. All you need to do to switch your syncs to HTTPS is:

      $ cd ~/path/to/checkout
      $ fossil sync


## Step 7: Renewing Automatically

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


-----------

<a id=”evolution”></a>
**Document Evolution**

Large parts of this article have been rewritten several times now due to
shifting technology in the TLS and proxying spheres.

There is no particularly good reason to expect that this sort of thing
will not continue to happen, so we consider this to be a living
document.  If you do not have commit access on the `fossil-scm.org`
repository to update this document as the world changes around it, you
can discuss this document [on the forum][fd].  This document’s author
keeps an eye on the forum and expects to keep this document updated with
ideas that appear in that thread.

[acme]: https://en.wikipedia.org/wiki/Automated_Certificate_Management_Environment
[cb]:   https://certbot.eff.org/
[cbnu]: https://certbot.eff.org/lets-encrypt/ubuntubionic-nginx
[fd]:   https://fossil-scm.org/forum/forumpost/ae6a4ee157
[hsts]: https://en.wikipedia.org/wiki/HTTP_Strict_Transport_Security
[lja]:  https://en.wikipedia.org/wiki/Logjam_(computer_security)
[mitm]: https://en.wikipedia.org/wiki/Man-in-the-middle_attack
[nest]: https://www.nginx.com/blog/http-strict-transport-security-hsts-and-nginx/
[ocsp]: https://en.wikipedia.org/wiki/OCSP_stapling
[qslc]: https://github.com/ssllabs/research/wiki/SSL-and-TLS-Deployment-Best-Practices
[qslt]: https://www.ssllabs.com/ssltest/
