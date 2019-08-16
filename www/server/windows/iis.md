# Serving via IIS

IIS offers several different ways of running Fossil, more than we cover
here. This document gives only the most widely useful options, in order
of complexity.


## CGI

*TODO*


## SCGI

*TODO*


## HTTP 

The following assumes you have the Fossil HTTP server running in the
background, serving some local repository, bound to localhost on a fixed
high-numbered TCP port. The following command in `cmd.exe` or PowerShell
will do that:

        fossil serve --port 9000 --localhost repo.fossil

That command assumes you’ve got `fossil.exe` in your `%PATH%` and you’re
in a directory holding `repo.fossil`. See [the platform-independent
instructions](../any/none.md) for further details.

The stock IIS setup doesn’t have reverse proxying features, but they’re
easily added through extensions. You will need to install the
[Application Request Routing][arr] and [URL Rewrite][ure] extensions. In
my testing here, URL Rewrite showed up immediately after installing it,
but I had to reboot the server to get ARR to show up. (Yay Windows.)

In IIS Manager:

1.  Double-click the “Application Request Routing Cache” icon.

2.  Right-click in the window that results, and select “Server Proxy
    Settings...”

3.  Check the “Enable Proxy” box in the dialog. Click the “Apply” text
    in the right-side pane.

4.  Return to the top server-level configuration area of IIS Manager and
    double-click the “URL Rewrite” icon. Alternately, you might find
    “URL Rewrite” in the right-side pane from within the ARR settings.

5.  Right click in the window that results and click “Add Rule(s)...”
    Tell it you want a “Blank rule” under “Inbound rules”.

6.  In the dialog that results, create a new rule called “Fossil repo
    proxy.” Set the “Pattern” to “`^(.*)$`” and “Rewrite URL” set to
    “`http://localhost:9000/{R:1}`”. That tells it to take everything in
    the path part of the URL and send it down to localhost:9000, where
    `fossil server` is listening.

7.  Click “Apply” in the right-side pane, then get back to the top level
    configuration for the server and click “Restart” in that same pane.

At this point, if you go to `http://localhost/` in your browser, you
should see your Fossil repository’s web interface instead of the default
IIS web site, as before you did all of the above.

This is a very simple configuration. You can do more complicated and
interesting things with this, such as redirecting only `/code` URLs to
Fossil, letting everything else go to IIS for normal web serving. You
could set up a mixed static and ASP.NET site on the rest of the URL
space, with Fossil serving only that one part. See the documentation on
[URL Rewrite rules][urr] for more ideas.


## Enabling TLS

Once you have one of the above options enabled, you might then want to
use IIS as an HTTPS proxy layer. For now, we’ll just send you to [the
docs][tls] for that.


[arr]: https://www.iis.net/downloads/microsoft/application-request-routing
[tls]: https://docs.microsoft.com/en-us/iis/manage/configuring-security/understanding-iis-url-authorization
[ure]: https://www.iis.net/downloads/microsoft/url-rewrite
[urr]: https://docs.microsoft.com/en-us/iis/extensions/url-rewrite-module/creating-rewrite-rules-for-the-url-rewrite-module
