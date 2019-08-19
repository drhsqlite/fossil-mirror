# Serving via althttpd

The public SQLite and Fossil web sites are not purely served by Fossil
for two reasons:

1.  We want access to these sites to be secured with TLS, which we do
    [via `stunnel`](./stunnel.md).

2.  Parts of these web sites are static, stored as plain files on disk,
    not as Fossil artifacts. We serve such files using a separate web
    server called [`althttpd`][ah], written by the primary author of
    both SQLite and Fossil, D. Richard Hipp. `althttpd` is a lightweight
    HTTP-only web server. It handles the static HTTP hits on
    <tt>sqlite.org</tt> and <tt>fossil-scm.org</tt>, delegating HTTPS
    hits to `stunnel` and dynamic content hits to Fossil [via
    CGI][cgi].

The largest single chunk of static content served directly by `althttpd`
rather than via Fossil is the [SQLite documentation][sd], which is built
[from source files][ds]. We don’t want those output files stored in
Fossil; we already keep that process’s *input* files in Fossil. Thus the
choice to serve the output statically.

In addition to the [server’s documentation page][ah], there is a large,
helpful header comment in the server’s [single-file C
implementation][ac]. Between that and the generic [Serving via CGI][cgi]
docs, you should be able to figure out how to serve Fossil via
`althttpd`.

*[Return to the top-level Fossil server article.](../)*


[ac]:  https://sqlite.org/docsrc/file/misc/althttpd.c
[ah]:  https://sqlite.org/docsrc/doc/trunk/misc/althttpd.md
[cgi]: ./cgi.md
[ds]:  https://sqlite.org/docsrc/
[sd]:  https://sqlite.org/docs.html
