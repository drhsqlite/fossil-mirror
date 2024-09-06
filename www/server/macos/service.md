# Serving via launchd on macOS

[`launchd`][ldhome] is the default service management framework on macOS
[since the release of Tiger in 2005][wpa]. If you want a Fossil server
to launch in the background on a Mac, it’s the way Apple wants you to do
it. `launchd` is to macOS as `systemd` is to most modern Linux desktop
systems. (Indeed, `systemd` arguably reinvented the perfectly good,
pre-existing `launchd` wheel.)

Unlike in [our `systemd` article](../debian/service.md), we’re not going
to show the per-user method here, because those so-called
[LaunchAgents][la] only start when a user is logged into the GUI, and
they stop when that user logs out. This does not strike us as proper
“server” behavior, so we’ll stick to system-level LaunchDaemons instead.

However, we will still give two different configurations, just as in the
`systemd` article: one for a standalone HTTP server, and one using
socket activation.

For more information on `launchd`, the single best resource we’ve found
is [launchd.info](https://launchd.info). The next best is:

    $ man launchd.plist

[la]:     http://www.grivet-tools.com/blog/2014/launchdaemons-vs-launchagents/
[ldhome]: https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html
[wpa]:    https://en.wikipedia.org/wiki/Launchd



## Standalone HTTP Server

To configure `launchd` to start Fossil as a standalone HTTP server,
write the following as `com.example.dev.FossilHTTP.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.example.dev.FossilHTTP</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/fossil</string>
        <string>server</string>
        <string>--port</string>
        <string>9000</string>
        <string>repo.fossil</string>
    </array>
    <key>WorkingDirectory</key>
    <string>/Users/you/museum</string>
    <key>KeepAlive</key>
    <true/>
    <key>RunAtLoad</key>
    <true/>
    <key>StandardErrorPath</key>
    <string>/tmp/fossil-error.log</string>
    <key>StandardOutPath</key>
    <string>/tmp/fossil-info.log</string>
    <key>UserName</key>
    <string>you</string>
    <key>GroupName</key>
    <string>staff</string>
    <key>InitGroups</key>
    <true/>
</dict>
</plist>
```

In this example, we’re assuming your development organization uses the
domain name “`dev.example.org`”, that your short macOS login name is
“`you`”, and that you store your Fossils in “`~/museum`”. Adjust these
elements of the plist file to suit your local situation.

You might be wondering about the use of `UserName`: isn’t Fossil
supposed to drop privileges and enter [a `chroot(2)`
jail](../../chroot.md) when it’s started as root like this? Why do we
need to give it a user name? Won’t Fossil use the owner of the
repository file to set that? All I can tell you is that in testing here,
if you leave the user and group configuration at the tail end of that
plist file out, Fossil will remain running as root!

Install that file and set it to start with:

    $ sudo install -o root -g wheel -m 644 com.example.dev.FossilHTTP.plist \
      /Library/LaunchDaemons/
    $ sudo launchctl load -w /Library/LaunchDaemons/com.example.dev.FossilHTTP.plist

Because we set the `RunAtLoad` key, this will also launch the daemon.

Stop the daemon with:

    $ sudo launchctl unload -w /Library/LaunchDaemons/com.example.dev.FossilHTTP.plist


## Socket Listener

Another useful method to serve a Fossil repo via `launchd` is by setting
up a socket listener:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.example.dev.FossilSocket</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/fossil</string>
        <string>http</string>
        <string>repo.fossil</string>
    </array>
    <key>Sockets</key>
    <dict>
        <key>Listeners</key>
        <dict>
            <key>SockServiceName</key>
            <string>9001</string>
            <key>SockType</key>
            <string>stream</string>
            <key>SockProtocol</key>
            <string>TCP</string>
            <key>SockFamily</key>
            <string>IPv4</string>
        </dict>
    </dict>
    <key>inetdCompatibility</key>
    <dict>
        <key>Wait</key>
        <false/>
    </dict>
    <key>WorkingDirectory</key>
    <string>/Users/you/museum</string>
    <key>UserName</key>
    <string>you</string>
    <key>GroupName</key>
    <string>staff</string>
    <key>InitGroups</key>
    <true/>
</dict>
</plist>
```

Save it as “`com.example.dev.FossilSocket.plist`” and install and load
it into `launchd` as above.

This version differs in several key ways:

1.  We’re calling Fossil as `fossil http` rather than `fossil server` to
    make it serve a single request and then shut down immediately.

2.  We’ve told `launchd` to listen on our TCP port number instead of
    passing it to `fossil`.

3.  We’re running the daemon in `inetd` compatibility mode of `launchd`
    with “wait” mode off, which tells it to attach the connected socket
    to the `fossil` process’s stdio handles.

4.  We’ve removed the `Standard*Path` keys because they interfere with
    our use of stdio handles for HTTP I/O. You might therefore want to
    start with the first method and then switch over to this one only
    once you’ve got the daemon launching debugged, since once you tie up
    stdio this way, you won’t be able to get logging information from
    Fossil via that path. (Fossil does have some internal logging
    mechanisms, but you can’t get at them until Fossil is launching!)

5.  We’ve removed the `KeepAlive` and `RunAtLoad` keys because those
    options aren’t appropriate to this type of service.

6.  Because we’re running it via a socket listener instead of as a
    standalone HTTP server, the Fossil service only takes system
    resources when it’s actually handling an HTTP hit.  If your Fossil
    server is mostly idle, this method will be a bit more efficient than
    the first option.


*[Return to the top-level Fossil server article.](../)*
