# Serving via httpd on OpenBSD

[`httpd`][httpd] is the default web server that is included in the base
install on OpenBSD. It's minimal and lightweight but secure and capable,
and provides a clean interface for setting up a Fossil server using
FastCGI.

This article will detail the steps required to setup a TLS-enabled
`httpd` configuration that serves multiple Fossil repositories out of
a single directory within a chroot, and allow `ssh` access to create
new repositories remotely.

**NOTE:** The following instructions assume an OpenBSD 6.7 installation.

[httpd]: https://www.openbsd.org/papers/httpd-asiabsdcon2015.pdf

## <a id="fslinstall"></a>Install Fossil

Use the OpenBSD package manager `pkg_add` to install Fossil, making sure
to select the statically linked binary.

```console
$ doas pkg_add fossil
quirks-3.325 signed on 2020-06-12T06:24:53Z
Ambiguous: choose package for fossil
      0: <None>
      1: fossil-2.10v0
      2: fossil-2.10v0-static
Your choice: 2
fossil-2.10v0-static: ok
```

This installs Fossil into the chroot. To facilitate local use, create a
symbolic link of the fossil executable into `/usr/local/bin`.

```console
$ doas ln -s /var/www/bin/fossil /usr/local/bin/fossil
```

As a privileged user, create the file `/var/www/cgi-bin/scm` with the
following contents to make the CGI script that `httpd` will execute in
response to `fsl.domain.tld` requests; all paths are relative to the
`/var/www` chroot.

```sh
#!/bin/fossil
directory: /htdocs/fsl.domain.tld
notfound: https://domain.tld
repolist
errorlog: /logs/fossil.log
```

The `directory` directive instructs Fossil to serve all repositories
found in `/var/www/htdocs/fsl.domain.tld`, while `errorlog` sets logging
to be saved to `/var/www/logs/fossil.log`; create the repository
directory and log file—making the latter owned by the `www` user, and
the script executable.

```console
$ doas mkdir /var/www/htdocs/fsl.domain.tld
$ doas touch /var/www/logs/fossil.log
$ doas chown www /var/www/logs/fossil.log
$ doas chmod 660 /var/www/logs/fossil.log
$ doas chmod 755 /var/www/cgi-bin/scm
```

## <a id="chroot"></a>Setup chroot

Fossil needs both `/dev/random` and `/dev/null`, which aren't accessible
from within the chroot, so need to be constructed; `/var`, however, is
mounted with the `nodev` option. Rather than removing this default
setting, create a small memory filesystem and then mount it on to
`/var/www/dev` with [`mount_mfs(8)`][mfs] so that the `random` and
`null` device files can be created. In order to avoid necessitating a
startup script to recreate the device files at boot, create a template
of the needed ``/dev`` tree to automatically populate the memory
filesystem.

```console
$ doas mkdir /var/www/dev
$ doas install -d -g daemon /template/dev
$ cd /template/dev
$ doas /dev/MAKEDEV urandom
$ doas mknod -m 666 null c 2 2
$ doas mount_mfs -s 1M -P /template/dev /dev/sd0b /var/www/dev
$ ls -l
total 0
crw-rw-rw-  1 root  daemon    2,   2 Jun 20 08:56 null
lrwxr-xr-x  1 root  daemon         7 Jun 18 06:30 random@ -> urandom
crw-r--r--  1 root  wheel    45,   0 Jun 18 06:30 urandom
```

[mfs]: https://man.openbsd.org/mount_mfs.8

To make the mountable memory filesystem permanent, open `/etc/fstab` as
a privileged user and add the following line to automate creation of the
filesystem at startup:

```console
swap /var/www/dev mfs rw,-s=1048576,-P=/template/dev 0 0
```

The same user that executes the fossil binary must have writable access
to the repository directory that resides within the chroot; on OpenBSD
this is `www`. In addition, grant repository directory ownership to the
user who will push to, pull from, and create repositories.

```console
$ doas chown -R user:www /var/www/htdocs/fsl.domain.tld
$ doas chmod 770 /var/www/htdocs/fsl.domain.tld
```

## <a id="httpdconfig"></a>Configure httpd

On OpenBSD, [httpd.conf(5)][httpd] is the configuration file for
`httpd`. To setup the server to serve all Fossil repositores within the
directory specified in the CGI script, and automatically redirect
standard HTTP requests to HTTPS—apart from [Let's Encrypt][LE]
challenges issued in response to [acme-client(1)][acme] certificate
requests—create `/etc/httpd.conf` as a privileged user with the
following contents.

[LE]: https://letsencrypt.org
[acme]: https://man.openbsd.org/acme-client.1
[httpd.conf(5)]: https://man.openbsd.org/httpd.conf.5

```apache
server "fsl.domain.tld" {
    listen on * port http
    root "/htdocs/fsl.domain.tld"
    location "/.well-known/acme-challenge/*" {
        root "/acme"
        request strip 2
    }
    location * {
        block return 301 "https://$HTTP_HOST$REQUEST_URI"
    }
    location  "/*" {
        fastcgi { param SCRIPT_FILENAME "/cgi-bin/scm" }
    }
}

server "fsl.domain.tld" {
    listen on * tls port https
    root "/htdocs/fsl.domain.tld"
    tls {
        certificate "/etc/ssl/domain.tld.fullchain.pem"
        key "/etc/ssl/private/domain.tld.key"
    }
    hsts {
        max-age 15768000
        preload
        subdomains
    }
    connection max request body 104857600
    location  "/*" {
        fastcgi { param SCRIPT_FILENAME "/cgi-bin/scm" }
    }
    location "/.well-known/acme-challenge/*" {
        root "/acme"
        request strip 2
    }
}
```

[The default limit][dlim] for HTTP messages in OpenBSD’s `httpd` server
is 1 MiB. Fossil chunks its sync protocol such that this is not
normally a problem, but when sending [unversioned content][uv], it uses
a single message for the entire file. Therefore, if you will be storing
files larger than this limit as unversioned content, you need to raise
the limit as we’ve done above with the “`connection max request body`”
setting, raising the limit to 100 MiB.

[dlim]: https://man.openbsd.org/httpd.conf.5#connection
[uv]:   ../../unvers.wiki

**NOTE:** If not already in possession of an HTTPS certificate, comment
out the `https` server block and proceed to securing a free
[Let's Encrypt Certificate](#letsencrypt); otherwise skip to
[Start `httpd`](#starthttpd).


## <a id="letsencrypt"></a>Let's Encrypt Certificate

In order for `httpd` to serve HTTPS, secure a free certificate from
Let's Encrypt using `acme-client`. Before issuing the request, however,
ensure you have a zone record for the subdomain with your registrar or
nameserver. Then open `/etc/acme-client.conf` as a privileged user to
configure the request.

```dosini
authority letsencrypt {
    api url "https://acme-v02.api.letsencrypt.org/directory"
    account key "/etc/acme/letsencrypt-privkey.pem"
}

authority letsencrypt-staging {
    api url "https://acme-staging.api.letsencrypt.org/directory"
    account key "/etc/acme/letsencrypt-staging-privkey.pem"
}

domain domain.tld {
    alternative names { www.domain.tld fsl.domain.tld }
    domain key "/etc/ssl/private/domain.tld.key"
    domain certificate "/etc/ssl/domain.tld.crt"
    domain full chain certificate "/etc/ssl/domain.tld.fullchain.pem"
    sign with letsencrypt
}
```

Start `httpd` with the new configuration file, and issue the certificate
request.

```console
$ doas rcctl start httpd
$ doas acme-client -vv domain.tld
acme-client: /etc/acme/letsencrypt-privkey.pem: account key exists (not creating)
acme-client: /etc/acme/letsencrypt-privkey.pem: loaded RSA account key
acme-client: /etc/ssl/private/domain.tld.key: generated RSA domain key
acme-client: https://acme-v01.api.letsencrypt.org/directory: directories
acme-client: acme-v01.api.letsencrypt.org: DNS: 172.65.32.248
...
N(Q????Z???j?j?>W#????b???? H????eb??T??*? DNosz(???n{L}???D???4[?B] (1174 bytes)
acme-client: /etc/ssl/domain.tld.crt: created
acme-client: /etc/ssl/domain.tld.fullchain.pem: created
```

A successful result will output the public certificate, full chain of
trust, and private key into the `/etc/ssl` directory as specified in
`acme-client.conf`.

```console
$ doas ls -lR /etc/ssl
-r--r--r--   1 root  wheel   2.3K Mar  2 01:31:03 2018 domain.tld.crt
-r--r--r--   1 root  wheel   3.9K Mar  2 01:31:03 2018 domain.tld.fullchain.pem

/etc/ssl/private:
-r--------  1 root  wheel   3.2K Mar  2 01:31:03 2018 domain.tld.key
```

Make sure to reopen `/etc/httpd.conf` to uncomment the second server
block responsible for serving HTTPS requests before proceeding.

## <a id="starthttpd"></a>Start `httpd`

With `httpd` configured to serve Fossil repositories out of
`/var/www/htdocs/fsl.domain.tld`, and the certificates and key in place,
enable and start `slowcgi`—OpenBSD's FastCGI wrapper server that will
execute the above Fossil CGI script—before checking that the syntax of
the `httpd.conf` configuration file is correct, and (re)starting the
server (if still running from requesting a Let's Encrypt certificate).

```console
$ doas rcctl enable slowcgi
$ doas rcctl start slowcgi
slowcgi(ok)
$ doas httpd -vnf /etc/httpd.conf
configuration OK
$ doas rcctl start httpd
httpd(ok)
```

## <a id="clientconfig"></a>Configure Client

To facilitate creating new repositories and pushing them to the server,
add the following function to your `~/.cshrc` or `~/.zprofile` or the
config file for whichever shell you are using on your development box.

```sh
finit() {
    fossil init $1.fossil && \
    chmod 664 $1.fossil && \
    fossil open $1.fossil && \
    fossil user password $USER $PASSWD && \
    fossil remote-url https://$USER:$PASSWD@fsl.domain.tld/$1 && \
    rsync --perms $1.fossil $USER@fsl.domain.tld:/var/www/htdocs/fsl.domain.tld/ >/dev/null && \
    chmod 644 $1.fossil && \
    fossil ui
}
```

This enables a new repository to be made with `finit repo`, which will
create the fossil repository file `repo.fossil` in the current working
directory; by default, the repository user is set to the environment
variable `$USER`. It then opens the repository and sets the user
password to the `$PASSWD` environment variable (which you can either set
with `export PASSWD 'password'` on the command line or add to a
*secured* shell environment file), and the `remote-url` to
`https://fsl.domain.tld/repo` with the credentials of `$USER` who is
authenticated with `$PASSWD`. Finally, it `rsync`'s the file to the
server before opening the local repository in your browser where you can
adjust settings such as anonymous user access, and set pertinent
repository details. Thereafter, you can add files with `fossil add`, and
commit with `fossil ci -m 'commit message'` where Fossil, by default,
will push to the `remote-url`. It's suggested you read the
[Fossil documentation][documentation]; with a sane and consistent
development model, the system is much more efficient and cohesive than
`git`—so the learning curve is not steep at all.

[documentation]: https://fossil-scm.org/home/doc/trunk/www/permutedindex.html

*[Return to the top-level Fossil server article.](../)*
