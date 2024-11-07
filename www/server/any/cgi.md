# Serving via CGI

A Fossil server can be run from most ordinary web servers as a CGI
program.  This feature allows Fossil to seamlessly integrate into a
larger website.  The [self-hosting Fossil repository web
site](../../selfhost.wiki) is implemented using CGI.  See the
[How CGI Works](../../aboutcgi.wiki) page for background information
on the CGI protocol.

To run Fossil as CGI, create a CGI script (here called "repo") in the
CGI directory of your web server with content like this:

    #!/usr/bin/fossil
    repository: /home/fossil/repo.fossil

Adjust the paths appropriately.  It may be necessary to set certain
permissions on this file or to modify an `.htaccess` file or make other
server-specific changes.  Consult the documentation for your particular
web server. The following permissions are *normally* required, but,
again, may be different for a particular configuration:

*   The Fossil binary (`/usr/bin/fossil` in the example above)
    must be readable/executable.

*   *All* directories leading up to the Fossil binary must be readable
    by the process which executes the CGI.

*   The CGI script must be executable for the user under which it will
    run, which often differs from the one running the web server.
    Consult your site's documentation or the web server’s system
    administrator.

*   *All* directories leading to the CGI script must be readable by the
    web server.

*   The repository file *and* the directory containing it must be
    writable by the same account which executes the Fossil binary.
    (This might differ from the user the web server normally runs
    under.) The directory holding the repository file(s) needs to be
    writable so that SQLite can write its journal files. When using
    another access control system, such as AppArmor or SELinux, it may
    be necessary to explicitly permit that account to read and write
    the necessary files.

*   Fossil must be able to create temporary files in a
    [directory that varies by host OS](../../env-opts.md#temp). When the
    CGI process is operating [within a chroot](../../chroot.md),
    ensure that this directory exists and is readable/writeable by the
    user who executes the Fossil binary.

Once the CGI script is set up correctly, and assuming your server is
also set correctly, you should be able to access your repository with a
URL like: <b>http://mydomain.org/cgi-bin/repo</b> This is assuming you
are running a web server like Apache that uses a “`cgi-bin`” directory
for scripts like our “`repo`” example.

To serve multiple repositories from a directory using CGI, use the
"directory:" tag in the CGI script rather than "repository:".  You
might also want to add a "notfound:" tag to tell where to redirect if
the particular repository requested by the URL is not found:

    #!/usr/bin/fossil
    directory: /home/fossil/repos
    notfound: http://url-to-go-to-if-repo-not-found/

Once deployed, a URL like: <b>http://mydomain.org/cgi-bin/repo/XYZ</b>
will serve up the repository `/home/fossil/repos/XYZ.fossil` if it
exists.

Additional options available to the CGI script are [documented
separately](../../cgi.wiki).

#### CGI with Apache behind an Nginx proxy

For the case where the Fossil repositories live on a computer, itself behind
an Internet-facing machine that employs Nginx to reverse proxy HTTP(S) requests
and take care of the TLS part of the connections in a transparent manner for
the downstream web servers, the CGI parameter `HTTPS=on` might not be set.
However, Fossil in CGI mode needs it in order to generate the correct links.

Apache can be instructed to pass this parameter further to the CGI scripts for
TLS connections with a stanza like

    SetEnvIf X-Forwarded-Proto "https" HTTPS=on

in its config file section for CGI, provided that

    proxy_set_header  X-Forwarded-Proto $scheme;

has been be added in the relevant proxying section of the Nginx config file.

*[Return to the top-level Fossil server article.](../)*

#### Apache mod_cgi and `CONTENT_LENGTH`

At some point in its 2.4.x family, Apache's `mod_cgi` stopped relaying
the Content-Length header in the HTTP reply from CGIs back to clients.
However, Fossil clients prior to 2024-04-17 depended on seeing the
Content-Length header and were unable to handle HTTP replies without
one.  The change in Apache behavior caused "fossil clone" and "fossil
sync" to stop working.  There are two possible fixes to this problem:

  1.  Restore legacy behavior in Apache by adding
      the following to the Apache configuration, scoped to the `<Directory>`
      entry or entries in which fossil is being served via CGI:
      <blockquote><pre>
      &lt;Directory ...&gt;
         SetEnv ap_trust_cgilike_cl "yes"
      &lt;/Directory&gt;
      </pre></blockquote>

  2.  Upgrade your Fossil client to any trunk check-in after 2024-04-17,
      as Fossil was upgraded to be able to deal with the missing
      Content-Length field by
      [check-in a8e33fb161f45b65](/info/a8e33fb161f45b65).
