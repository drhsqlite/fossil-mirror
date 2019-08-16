# Serving via CGI

A Fossil server can be run from most ordinary web servers as a CGI
program.  This feature allows Fossil to seamlessly integrate into a
larger website.  We use CGI for the [self-hosting Fossil repository web
site](../../selfhost.wiki).

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
    writable so that SQLite can write its journal files.

*   Fossil must be able to create temporary files in a
    [directory that varies by host OS](../../env-opts.md#temp). When the
    CGI process is operating [within a chroot](../../server.wiki#chroot),
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
