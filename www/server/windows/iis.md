# Serving via IIS

## Why Bother?

The first part of the scheme below sets Fossil up as an HTTP server, so
you might be wondering why you wouldn’t just modify that to make it
listen on all network interfaces on TCP port 80, so you can avoid the
need for IIS entirely. For simple use cases, you can indeed do without
IIS, but there are several use cases where adding it is helpful:

1.  Proxying Fossil with IIS lets you [add TLS encryption][tls], which
    [Fossil does not currently speak](../../ssl.wiki) in its server role.

2.  The URL rewriting we do below allows Fossil to be part of a larger
    site already being served with IIS.

3.  You can have a mixed-mode site, with Fossil acting as a powerful
    dynamic content management service and IIS as a fast static content
    server.  The pure-Fossil alternative requires that you check all of
    your static content into Fossil as versioned or unversioned
    artifacts.

This article shows how you can get any combination of those benefits by
using IIS as a reverse proxy for `fossil server`.

There are other ways to use IIS to serve Fossil, such as [via
CGI](./cgi.md).


## Background Fossil Service Setup

You will need to have the Fossil HTTP server running in the background,
serving some local repository, bound to localhost on a fixed
high-numbered TCP port. For the purposes of testing, simply start it by
hand in your command shell of choice:

    fossil serve --port 9000 --localhost repo.fossil

That command assumes you’ve got `fossil.exe` in your `%PATH%` and you’re
in a directory holding `repo.fossil`. See [the platform-independent
instructions](../any/none.md) for further details.

For a more robust setup, we recommend that you [install Fossil as a
Windows service](./service.md), which will allow Fossil to start at
system boot, before anyone has logged in interactively.


## <a id="install"></a>Install IIS

IIS might not be installed in your system yet, so follow the path
appropriate to your host OS.  We’ve tested only the latest Microsoft
OSes as of the time of this writing, but the basic process should be
similar on older OSes.


### Windows Server 2019

1.  Start “Server Manager”
2.  Tell it you want to “Add roles and features”
3.  Select “Role-based or feature-based installation”
4.  Select your local server
5.  In the Server Roles section, enable “Web Server (IIS)”

### Windows 

1.  Open Control Panel
2.  Go to “Programs”
3.  Select “Turn Windows features on or off” in the left-side pane
4.  In the “Windows Features” dialog, enable “Internet Information
    Services”

The default set of IIS features there will suffice for this tutorial,
but you might want to enable additional features.


## Setting up the Proxy

The stock IIS setup doesn’t have reverse proxying features, but they’re
easily added through extensions. You will need to install the
[Application Request Routing][arr] and [URL Rewrite][ure] extensions. In
my testing here, URL Rewrite showed up immediately after installing it,
but I had to reboot the server to get ARR to show up. (Yay Windows.)

You can install these things through the direct links above, or you can
do it via the Web Platform Installer feature of IIS Manager (a.k.a.
`INETMGR`).

Set these extensions up in IIS Manager like so:

1.  Double-click the “Application Request Routing Cache” icon.

2.  Right-click in the window that results, and select “Server Proxy
    Settings...”

3.  Check the “Enable Proxy” box in the dialog. Click the “Apply” text
    in the right-side pane.

4.  Return to the top server-level configuration area of IIS Manager and
    double-click the “URL Rewrite” icon. Alternately, you might find
    “URL Rewrite” in the right-side pane from within the ARR settings.

5.  Right click in the window that results, and click “Add Rule(s)...”
    Tell it you want a “Blank rule” under “Inbound rules”.

6.  In the dialog that results, create a new rule called “Fossil repo
    proxy.” Set the “Pattern” to “`^(.*)$`” and “Rewrite URL” set to
    “`http://localhost:9000/{R:1}`”. That tells it to take everything in
    the path part of the URL and send it down to localhost:9000, where
    `fossil server` is listening.

7.  Click “Apply” in the right-side pane, then get back to the top level
    configuration for the server, and click “Restart” in that same pane.

At this point, if you go to `http://localhost/` in your browser, you
should see your Fossil repository’s web interface instead of the default
IIS web site, as before you did all of the above.

This is a very simple configuration. You can do more complicated and
interesting things with this, such as redirecting only `/code` URLs to
Fossil by setting the Pattern in step 6 to “`^/code(.*)$`”. (You would
also need to pass `--baseurl http://example.com/code` in the `fossil
server` command to make this work properly.) IIS would then directly
serve all other URLs. You could also intermix ASP.NET applications in
the URL scheme in this way.

See the documentation on [URL Rewrite rules][urr] for more ideas.

*[Return to the top-level Fossil server article.](../)*


[arr]: https://www.iis.net/downloads/microsoft/application-request-routing
[tls]: https://docs.microsoft.com/en-us/iis/manage/configuring-security/understanding-iis-url-authorization
[ure]: https://www.iis.net/downloads/microsoft/url-rewrite
[urr]: https://docs.microsoft.com/en-us/iis/extensions/url-rewrite-module/creating-rewrite-rules-for-the-url-rewrite-module
