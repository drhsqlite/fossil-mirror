# OCI Containers

This document shows how to build Fossil into [OCI] compatible containers
and how to use those containers in interesting ways. We start off using
the original and still most popular container development and runtime
platform, [Docker], but since you have more options than that, we will
show some of these options later on.

[Docker]: https://www.docker.com/
[OCI]:    https://opencontainers.org/


## 1. Quick Start

Fossil ships a `Dockerfile` at the top of its source tree which you can
build like so:

```
  $ docker build -t fossil .
```

If the image built successfully, you can create a container from it and
test that it runs:

```
  $ docker run --name fossil -p 9999:8080/tcp fossil
```

This shows us remapping the internal TCP listening port as 9999 on the
host. This feature of OCI runtimes means there’s little point to using
the “`fossil server --port`” feature inside the container. We can let
Fossil default to 8080 internally, then remap it to wherever we want it
on the host instead.

Our stock `Dockerfile` configures Fossil with the default feature set,
so you may wish to modify the `Dockerfile` to add configuration options,
add APK packages to support those options, and so forth.

The Fossil `Makefile` provides two convenience targets,
“`make container-image`” and “`make container-run`”. The first creates a
versioned container image, and the second does that and then launches a
fresh container based on that image. You can pass extra arguments to the
first command via the Makefile’s `DBFLAGS` variable and to the second
with the `DCFLAGS` variable. (DB is short for “`docker build`”, and DC
is short for “`docker create`”, a sub-step of the “run” target.)
To get the custom port setting as in
second command above, say:

```
  $ make container-run DCFLAGS='-p 9999:8080/tcp'
```

Contrast the raw “`docker`” commands above, which create an
_unversioned_ image called `fossil:latest` and from that a container
simply called `fossil`. The unversioned names are more convenient for
interactive use, while the versioned ones are good for CI/CD type
applications since they avoid a conflict with past versions; it lets you
keep old containers around for quick roll-backs while replacing them
with fresh ones.


## 2. <a id="storage"></a>Repository Storage Options

If you want the container to serve an existing repository, there are at
least two right ways to do it.

The wrong way is to use the `Dockerfile COPY` command, because by baking
the repo into the image at build time, it will become one of the image’s
base layers. The end result is that each time you build a container from
that image, the repo will be reset to its build-time state. Worse,
restarting the container will do the same thing, since the base image
layers are immutable in Docker. This is almost certainly not what you
want.

The correct ways put the repo into the _container_ created from the
_image_, not in the image itself.


### <a id="repo-inside"></a> 2.1 Storing the Repo Inside the Container

The simplest method is to stop the container if it was running, then
say:

```
  $ docker cp /path/to/my-project.fossil fossil:/jail/museum/repo.fossil
  $ docker start fossil
  $ docker exec fossil chown -R 499 /jail/museum
```

That copies the local Fossil repo into the container where the server
expects to find it, so that the “start” command causes it to serve from
that copied-in file instead. Since it lives atop the immutable base
layers, it persists as part of the container proper, surviving restarts.

Notice that the copy command changes the name of the repository
database. The container configuration expects it to be called
`repo.fossil`, which it almost certainly was not out on the host system.
This is because there is only one repository inside this container, so
we don’t have to name it after the project it contains, as is
traditional. A generic name lets us hard-code the server start command.

If you skip the “chown” command above and put “`http://localhost:9999/`”
into your browser, expecting to see the copied-in repo’s home page, you
will get an opaque “Not Found” error. This is because the user and group
ID of the file will be that of your local user on the container’s host
machine, which is unlikely to map to anything in the container’s
`/etc/passwd` and `/etc/group` files, effectively preventing the server
from reading the copied-in repository file. 499 is the default “`fossil`”
user ID inside the container, causing Fossil to run with that user’s
privileges after it enters the chroot. (See [below](#args) for how to
change this default.) You don’t have to restart the server after fixing
this with `chmod`: simply reload the browser, and Fossil will try again.


### 2.2 <a id="bind-mount"></a>Storing the Repo Outside the Container

The simple storage method above has a problem: Docker containers are
designed to be killed off at the slightest cause, rebuilt, and
redeployed. If you do that with the repo inside the container, it gets
destroyed, too. The solution is to replace the “run” command above with
the following:

```
  $ docker run \
    --publish 9999:8080 \
    --name fossil-bind-mount \
    --volume ~/museum:/jail/museum \
    fossil
```

Because this bind mount maps a host-side directory (`~/museum`) into the
container, you don’t need to `docker cp` the repo into the container at
all. It still expects to find the repository as `repo.fossil` under that
directory, but now both the host and the container can see that repo DB.

Instead of a bind mount, you could instead set up a separate [Docker
volume](https://docs.docker.com/storage/volumes/), at which point you
_would_ need to `docker cp` the repo file into the container.

Either way, files in these mounted directories have a lifetime
independent of the container(s) they’re mounted into. When you need to
rebuild the container or its underlying image — such as to upgrade to a
newer version of Fossil — the external directory remains behind and gets
remapped into the new container when you recreate it with `--volume/-v`.


#### 2.2.1 <a id="wal-mode"></a>WAL Mode Interactions

You might be aware that OCI containers allow mapping a single file into
the repository rather than a whole directory.  Since Fossil repositories
are specially-formatted SQLite databases, you might be wondering why we
don’t say things like:

```
  --volume ~/museum/my-project.fossil:/jail/museum/repo.fossil
```

That lets us have a convenient file name for the project outside the
container while letting the configuration inside the container refer to
the generic “`/museum/repo.fossil`” name. Why should we have to name
the repo generically on the outside merely to placate the container?

The reason is, you might be serving that repo with [WAL mode][wal]
enabled. If you map the repo DB alone into the container, the Fossil
instance inside the container will write the `-journal` and `-wal` files
alongside the mapped-in repository inside the container.  That’s fine as
far as it goes, but if you then try using the same repo DB from outside
the container while there’s an active WAL, the Fossil instance outside
won’t know about it. It will think it needs to write *its own*
`-journal` and `-wal` files *outside* the container, creating a high
risk of [database corruption][dbcorr].

If we map a whole directory, both sides see the same set of WAL files.
[Testing](https://tangentsoft.com/sqlite/dir/walbanger?ci=trunk)
gives us a reasonable level of confidence that using WAL across a
container boundary is safe when used in this manner.

[dbcorr]: https://www.sqlite.org/howtocorrupt.html#_deleting_a_hot_journal
[wal]:    https://www.sqlite.org/wal.html


## 3. <a id="security"></a>Security

### 3.1 <a id="chroot"></a>Why Chroot?

A potentially surprising feature of this container is that it runs
Fossil as root. Since that causes [the chroot jail feature](./chroot.md)
to kick in, and a Docker container is a type of über-jail already, you
may be wondering why we bother. Instead, why not either:

*   run `fossil server --nojail` to skip the internal chroot; or
*   set “`USER fossil`” in the `Dockerfile` so it starts Fossil as
    that user instead

The reason is, although this container is quite stripped-down by today’s
standards, it’s based on the [surprisingly powerful Busybox
project](https://www.busybox.net/BusyBox.html). (This author made a
living for years in the early 1990s using Unix systems that were less
powerful than this container.) If someone ever figured out how to make a
Fossil binary execute arbitrary commands on the host or to open up a
remote shell, the power available to them at that point would make it
likely that they’d be able to island-hop from there into the rest of
your network. That power is there for you as the system administrator
alone, to let you inspect the container’s runtime behavior, change
things on the fly, and so forth. Fossil proper doesn’t need that power;
if we take it away via this cute double-jail dance, we keep any
potential attacker from making use of it should they ever get in.

Having said this, know that we deem this risk low since a) it’s never
happened, that we know of; and b) we haven’t enabled any of the risky
features of Fossil such as [TH1 docs][th1docrisk]. Nevertheless, we
believe defense-in-depth strategies are wise.

If you say something like “`docker exec fossil ps`” while the system is
idle, it’s likely to report a single `fossil` process running as `root`
even though the chroot feature is documented as causing Fossil to drop
its privileges in favor of the owner of the repository database or its
containing folder. If the repo file is owned by the in-container user
“`fossil`”, why is the server still running as root?

It’s because you’re seeing only the parent process, which assumes it’s
running on bare metal or a VM and thus may need to do rootly things like
listening on port 80 or 443 before forking off any children to handle
HTTP hits. Fossil’s chroot feature only takes effect in these child
processes. This is why you can fix broken permissions with `chown`
after the container is already running, without restarting it: each hit
reevaluates the repository file permissions when deciding what user to
become when dropping root privileges.

[th1docrisk]: https://fossil-scm.org/forum/forumpost/42e0c16544


### 3.2 <a id="caps"></a>Dropping Unnecessary Capabilities

The example commands above create the container with [a default set of
Linux kernel capabilities][defcap]. Although Docker strips away almost
all of the traditional root capabilities by default, and Fossil doesn’t
need any of those it does take away, Docker does leave some enabled that
Fossil doesn’t actually need. You can tighten the scope of capabilities
by adding “`--cap-drop`” options to your container creation commands.

Specifically:

*   **`AUDIT_WRITE`**: Fossil doesn’t write to the kernel’s auditing
    log, and we can’t see any reason you’d want to be able to do that as
    an administrator shelled into the container, either. Auditing is
    something done on the host, not from inside each individual
    container.

*   **`CHOWN`**: The Fossil server never even calls `chown(2)`, and our
    image build process sets up all file ownership properly, to the
    extent that this is possible under the limitations of our
    automation.

    Curiously, stripping this capability doesn’t affect your ability to
    run commands like “`chown -R fossil:fossil /jail/museum`” when
    you’re using bind mounts or external volumes — as we recommend
    [above](#bind-mount) — because it’s the host OS’s kernel
    capabilities that affect the underlying `chown(2)` call in that
    case, not those of the container.

    If for some reason you did have to change file ownership of
    in-container files, it’s best to do that by changing the
    `Dockerfile` to suit, then rebuilding the container, since that
    bakes the need for the change into your reproducible build process.
    If you had to do it without rebuilding the container, [there’s a
    workaround][capchg] for the fact that capabilities are a create-time
    change, baked semi-indelibly into the container configuration.

*    **`FSETID`**: Fossil doesn’t use the SUID and SGID bits itself, and
    our build process doesn’t set those flags on any of the files.
    Although the second fact means we can’t see any harm from leaving
    this enabled, we also can’t see any good reason to allow it, so we
    strip it.

*    **`KILL`**: The only place Fossil calls `kill(2)` is in the
    [backoffice], and then only for processes it created on earlier
    runs; it doesn’t need the ability to kill processes created by other
    users. You might wish for this ability as an administrator shelled
    into the container, but you can pass the “`docker exec --user`”
    option to run commands within your container as the legitimate owner
    of the process, removing the need for this capability.

*    **`MKNOD`**: All device nodes are created at build time and are
    never changed at run time. Realize that the virtualized device nodes
    inside the container get mapped onto real devices on the host, so if
    an attacker ever got a root shell on the container, they might be
    able to do actual damage to the host if we didn’t preemptively strip
    this capability away.

*    **`NET_BIND_SERVICE`**: With containerized deployment, Fossil never
    needs the ability to bind the server to low-numbered TCP ports, not
    even if you’re running the server in production with TLS enabled and
    want the service bound to port 443. It’s perfectly fine to let the
    Fossil instance inside the container bind to its default port (8080)
    because you can rebind it on the host with the
    “`docker create --publish 443:8080`” option. It’s the container’s
    _host_ that needs this ability, not the container itself.

    (Even the container runtime might not need that capability if you’re
    [terminating TLS with a front-end proxy](./ssl.wiki#server). You’re
    more likely to say something like “`-p localhost:12345:8080`” and then
    configure the reverse proxy to translate external HTTPS calls into
    HTTP directed at this internal port 12345.)

*    **`NET_RAW`**: Fossil itself doesn’t use raw sockets, and our build
    process leaves out all the Busybox utilities that require them.
    Although that set includes common tools like `ping`, we foresee no
    compelling reason to use that or any of these other elided utilities
    — `ether-wake`, `netstat`, `traceroute`, and `udhcp` — inside the
    container. If you need to ping something, do it on the host.

    If we did not take this hard-line stance, an attacker that broke
    into the container and gained root privileges might use raw sockets
    to do a wide array of bad things to any network the container is
    bound to.

*    **`SETFCAP, SETPCAP`**: There isn’t much call for file permission
    granularity beyond the classic Unix ones inside the container, so we
    drop root’s ability to change them.

All together, we recommend adding the following options to your
“`docker run`” commands, as well as to any “`docker create`” command
that will be followed by “`docker start`”:

```
  --cap-drop AUDIT_WRITE \
  --cap-drop CHOWN \
  --cap-drop FSETID \
  --cap-drop KILL \
  --cap-drop MKNOD \
  --cap-drop NET_BIND_SERVICE \
  --cap-drop NET_RAW \
  --cap-drop SETFCAP \
  --cap-drop SETPCAP
```

In the next section, we’ll show a case where you create a container
without ever running it, making these options pointless.

[backoffice]: ./backoffice.md
[defcap]:     https://docs.docker.com/engine/security/#linux-kernel-capabilities
[capchg]:     https://stackoverflow.com/a/45752205/142454



## 4. <a id="static"></a>Extracting a Static Binary

Our 2-stage build process uses Alpine Linux only as a build host. Once
we’ve got everything reduced to the two key static binaries — Fossil and
BusyBox — we throw all the rest of it away.

A secondary benefit falls out of this process for free: it’s arguably
the easiest way to build a purely static Fossil binary for Linux. Most
modern Linux distros make this surprisingly difficult, but Alpine’s
back-to-basics nature makes static builds work the way they used to,
back in the day. If that’s all you’re after, you can do so as easily as
this:

```
  $ docker build -t fossil .
  $ docker create --name fossil-static-tmp fossil
  $ docker cp fossil-static-tmp:/jail/bin/fossil .
  $ docker container rm fossil-static-tmp
```

The resulting binary is the single largest file inside that container,
at about 6 MiB. (It’s built stripped.)


## 5. <a id="args"></a>Container Build Arguments

### <a id="pkg-vers"></a> 5.1 Package Versions

You can override the default versions of Fossil and BusyBox that get
fetched in the build step. To get the latest-and-greatest of everything,
you could say:

```
  $ docker build -t fossil \
    --build-arg FSLVER=trunk \
    --build-arg BBXVER=master .
```

(But don’t, for reasons we will get to.)

Because the BusyBox configuration file we ship was created with and
tested against a specific stable release, that’s the version we pull by
default. It does try to merge the defaults for any new configuration
settings into the stock set, but since it’s possible this will fail, we
don’t blindly update the BusyBox version merely because a new release
came out. Someone needs to get around to vetting it against our stock
configuration first.

As for Fossil, it defaults to fetching the same version as the checkout
you’re running the build command from, based on checkin ID. You could
use this to get a release build, for instance:

```
  $ docker build -t fossil \
    --build-arg FSLVER=version-2.20 .
```

Or equivalently, using Fossil’s `Makefile` convenience target:

```
  $ make container-image \
    DBFLAGS='--build-arg FSLVER=version-2.20'
```

While you could instead use the generic
“`release`” tag here, it’s better to use a specific version number
since Docker caches downloaded files and tries to
reuse them across builds. If you ask for “`release`” before a new
version is tagged and then immediately after, you might expect to get
two different tarballs, but because the underlying source tarball URL
remains the same when you do that, you’ll end up reusing the
old tarball from your Docker cache. This will occur
even if you pass the “`docker build --no-cache`” option.

This is why we default to pulling the Fossil tarball by checkin ID
rather than let it default to the generic “`trunk`” tag: so the URL will
change each time you update your Fossil source tree, forcing Docker to
pull a fresh tarball.


### 5.2 <a id="uids"></a>User & Group IDs

The “`fossil`” user and group IDs inside the container default to 499.
Why? Regular user IDs start at 500 or 1000 on most Unix type systems,
leaving those below it for system users like this Fossil daemon owner.
Since it’s typical for these to start at 0 and go upward, we started at
500 and went *down* one instead to reduce the chance of a conflict to as
close to zero as we can manage.

To change it to something else, say:

```
  $ make container-image \
    DBFLAGS='--build-arg UID=501'
```

This is particularly useful if you’re putting your repository on a
Docker volume since the IDs “leak” out into the host environment via
file permissions. You may therefore wish them to mean something on both
sides of the container barrier rather than have “499” appear on the host
in “`ls -l`” output.


### 5.3 <a id="config"></a>Fossil Configuration Options

You can use this same mechanism to enable non-default Fossil
configuration options in your build. For instance, to turn on
the JSON API and the TH1 docs extension:

```
  $ make container-image \
    DBFLAGS='--build-arg FSLCFG="--json --with-th1-docs"'
```

If you also wanted [the Tcl evaluation extension](./th1.md#tclEval),
that’s trickier because it requires the `tcl-dev` package to be
installed atop Alpine Linux in the first image build stage. We don’t
currently have a way to do that because it brings you to a new problem:
Alpine provides only a dynamic Tcl library, which conflicts with our
wish for [a static Fossil binary](#static). For those who want such a
“batteries included” container, we recommend taking a look at [this
alternative](https://hub.docker.com/r/duvel/fossil); needless to say,
it’s inherently less secure than our stock container, but you may find
the tradeoff worthwhile.


## 6. <a id="light"></a>Lightweight Alternatives to Docker

Those afflicted with sticker shock at seeing the size of a [Docker
Desktop][DD] installation — 1.65 GB here — might’ve immediately
“noped” out of the whole concept of containers. The first thing to
realize is that when it comes to actually serving simple containers like
the ones shown above is that [Docker Engine][DE] suffices, at about a
quarter of the size.

Yet on a small server — say, a $4/month 10 GiB Digital Ocean droplet —
that’s still a big chunk of your storage budget. It takes 100:1 overhead
just to run a 4 MiB Fossil server container? Once again, I wouldn’t
blame you if you noped right on out of here, but if you will be patient,
you will find that there are ways to run Fossil inside a container even
on entry-level cloud VPSes. These are well-suited to running Fossil; you
don’t have to resort to [raw Fossil service][srv] to succeed,
leaving the benefits of containerization to those with bigger budgets.

For the sake of simple examples in this section, we’ll assume you’re
integrating Fossil into a larger web site, such as with our [Debian +
nginx + TLS][DNT] plan. This is why all of the examples below create
the container with this option:

```
  --publish 127.0.0.1:9999:8080
```

The assumption is that there’s a reverse proxy running somewhere that
redirects public web hits to localhost port 9999, which in turn goes to
port 8080 inside the container.  This use of Docker/Podman port
publishing effectively replaces the use of the
“`fossil server --localhost`” option.

For the nginx case, you need to add `--scgi` to these commands, and you
might also need to specify `--baseurl`.

Containers are a fine addition to such a scheme as they isolate the
Fossil sections of the site from the rest of the back-end resources,
thus greatly reducing the chance that they’ll ever be used to break into
the host as a whole.

(If you wanted to be double-safe, you could put the web server into
another container, restricting it to reading from the static web
site directory and connecting across localhost to back-end dynamic
content servers such as Fossil. That’s way outside the scope of this
document, but you can find ready advice for that elsewhere. Seeing how
we do this with Fossil should help you bridge the gap in extending
this idea to the rest of your site.)

[DD]:  https://www.docker.com/products/docker-desktop/
[DE]:  https://docs.docker.com/engine/
[DNT]: ./server/debian/nginx.md
[srv]: ./server/


### 6.1 <a id="nerdctl" name="containerd"></a>Stripping Docker Engine Down

The core of Docker Engine is its [`containerd`][ctrd] daemon and the
[`runc`][runc] container runner. Add to this the out-of-core CLI program
[`nerdctl`][nerdctl] and you have enough of the engine to run Fossil
containers. The big things you’re missing are:

*   **BuildKit**: The container build engine, which doesn’t matter if
    you’re building elsewhere and using a container registry as an
    intermediary between that build host and the deployment host.

*   **SwarmKit**: A powerful yet simple orchestrator for Docker that you
    probably aren’t using with Fossil anyway.

In exchange, you get a runtime that’s about half the size of Docker
Engine. The commands are essentially the same as above, but you say
“`nerdctl`” instead of “`docker`”. You might alias one to the other,
because you’re still going to be using Docker to build and ship your
container images.

[ctrd]:    https://containerd.io/
[nerdctl]: https://github.com/containerd/nerdctl
[runc]:    https://github.com/opencontainers/runc


### 6.2 <a id="podman"></a>Podman

A lighter-weight alternative to either of the prior options that doesn’t
give up the image builder is [Podman]. Initially created by
Red Hat and thus popular on that family of OSes, it will run on
any flavor of Linux. It can even be made to run [on macOS via Homebrew][pmmac]
or [on Windows via WSL2][pmwin].

On Ubuntu 22.04, the installation size is about 38&nbsp;MiB, roughly a
tenth the size of Docker Engine.

Although Podman [bills itself][whatis] as a drop-in replacement for the
`docker` command and everything that sits behind it, some of the tool’s
design decisions affect how our Fossil containers run, as compared to
using Docker. The most important of these is that, by default, Podman
wants to run your container “rootless,” meaning that it runs as a
regular user.  This is generally better for security, but [we dealt with
that risk differently above](#chroot) already. Since neither choice is
unassailably correct in all conditions, we’ll document both options
here.

[pmmac]:  https://podman.io/getting-started/installation.html#macos
[pmwin]:  https://github.com/containers/podman/blob/main/docs/tutorials/podman-for-windows.md
[Podman]: https://podman.io/
[whatis]: https://podman.io/whatis.html


#### 6.2.1 <a id="podman-rootless"></a>Fossil in a Rootless Podman Container

If you build the stock Fossil container under `podman`, it will fail at
two key steps:

1.  The `mknod` calls in the second stage, which create the `/jail/dev`
    nodes. For a rootless container, we want it to use the “real” `/dev`
    tree mounted into the container’s root filesystem instead.

2. Anything that depends on the `/jail` directory and the fact that it
   becomes the file system’s root once the Fossil server is up and running.

[The changes to fix this](/file/containers/Dockerfile-nojail.patch)
aren’t complicated. Simply apply that patch to our stock `Dockerfile`
and rebuild:

```
  $ patch -p0 < containers/Dockerfile-nojail.patch
  $ docker build -t fossil:nojail .
  $ docker create \
    --name fossil-nojail \
    --publish 127.0.0.1:9999:8080 \
    --volume ~/museum:/museum \
    fossil:nojail
```

Do realize that by doing this, if an attacker ever managed to get shell
access on your container, they’d have a BusyBox installation to play
around in. That shouldn’t be enough to let them break out of the
container entirely, but they’ll have powerful tools like `wget`, and
they’ll be connected to the network the container runs on. Once the bad
guy is inside the house, he doesn’t necessarily have to go after the
residents directly to cause problems for them.


#### 6.2.2 <a id="podman-rootful"></a>Fossil in a Rootful Podman Container

##### Simple Method

Fortunately, it’s easy enough to have it both ways. Simply run your
`podman` commands as root:

```
  $ sudo podman build -t fossil --cap-add MKNOD .
  $ sudo podman create \
    --name fossil \
    --cap-drop CHOWN \
    --cap-drop FSETID \
    --cap-drop KILL \
    --cap-drop NET_BIND_SERVICE \
    --cap-drop SETFCAP \
    --cap-drop SETPCAP \
    --publish 127.0.0.1:9999:8080 \
    localhost/fossil
  $ sudo podman start fossil
```

It’s obvious why we have to start the container as root, but why create
and build it as root, too? Isn’t that a regression from the modern
practice of doing as much as possible with a normal user?

We have to do the build under `sudo` in part because we’re doing rootly
things with the file system image layers we’re building up. Just because
it’s done inside a container runtime’s build environment doesn’t mean we
can get away without root privileges to do things like create the
`/jail/dev/null` node.

The other reason we need “`sudo podman build`” is because it puts the result
into root’s Podman image registry, where the next steps look for it.

That in turn explains why we need “`sudo podman create`:” because it’s
creating a container based on an image that was created by root. If you
ran that step without `sudo`, it wouldn’t be able to find the image.

If Docker is looking better and better to you as a result of all this,
realize that it’s doing the same thing. It just hides it better by
creating the `docker` group, so that when your user gets added to that
group, you get silent root privilege escalation on your build machine.
This is why Podman defaults to rootless containers.  If you can get away
with it, it’s a better way to work.  We would not be recommending
running `podman` under `sudo` if it didn’t buy us [something we wanted
badly](#chroot).

Notice that we had to add the ability to run `mknod(8)` during the
build. [Podman sensibly denies this by default][nomknod], which lets us
leave off the corresponding `--cap-drop` option. Podman also denies
`CAP_NET_RAW` and `CAP_AUDIT_WRITE` by default, which we don’t need, so
we’ve simply removed them from the `--cap-drop` list relative to the
commands for Docker above.

[nomknod]: https://github.com/containers/podman/issues/15626


##### <a id="pm-root-workaround"></a>Building Under Docker, Running Under Podman

If you have a remote host where the Fossil instance needs to run, it’s
possible to get around this need to build the image as root on the
remote system. You still have to build as root on the local system, but
as I said above, Docker already does this. What we’re doing is shifting
the risk of running as root from the public host to the local one.

Once you have the image built on the local machine, create a “`fossil`”
repository on your container repository of choice such as [Docker
Hub](https://hub.docker.com), then say:

```
  $ docker login
  $ docker tag fossil:latest mydockername/fossil:latest
  $ docker image push mydockername/fossil:latest
```

That will push the image up to your account, so that you can then switch
to the remote machine and say:

```
  $ sudo podman create \
    --any-options-you-like \
    docker.io/mydockername/fossil
```

This round-trip through the public image registry has another side
benefit: your local system might be a lot faster than your remote one,
as when the remote is a small VPS. Even with the overhead of schlepping
container images across the Internet, it can be a net win in terms of
build time.



### 6.3 <a id="nspawn"></a>`systemd-container`

If even the Podman stack is too big for you, the next-best option I’m
aware of is the `systemd-container` infrastructure on modern Linuxes,
available since version 239 or so.  Its runtime tooling requires only
about 1.4 MiB of disk space:

```
  $ sudo apt install systemd-container btrfs-tools
```

That command assumes the primary test environment for
this guide, Ubuntu 22.04 LTS with `systemd` 249.  For best
results, `/var/lib/machines` should be a btrfs volume, because
[`$REASONS`][mcfad]. For CentOS Stream 9 and other Red Hattish
systems, you will have to make several adjustments, which we’ve
collected [below](#nspawn-centos) to keep these examples clear.

We’ll assume your Fossil repository stores something called
“`myproject`” within `~/museum/myproject/repo.fossil`, named according
to the reasons given [above](#repo-inside). We’ll make consistent use of
this naming scheme in the examples below so that you will be able to
replace the “`myproject`” element of the various file and path names.

The first configuration step is to convert the Docker container into
a “machine,” as `systemd` calls it.  The easiest method is:

```
  $ make container
  $ docker container export $(make container-version) |
    machinectl import-tar - myproject
```

Next, create `/etc/systemd/nspawn/myproject.nspawn`, containing
something like:

----

```
[Exec]
WorkingDirectory=/jail
Parameters=bin/fossil server                \
    --baseurl https://example.com/myproject \
    --chroot /jail                          \
    --create                                \
    --jsmode bundled                        \
    --localhost                             \
    --port 9000                             \
    --scgi                                  \
    --user admin                            \
    museum/repo.fossil
DropCapability=          \
    CAP_AUDIT_WRITE      \
    CAP_CHOWN            \
    CAP_FSETID           \
    CAP_KILL             \
    CAP_MKNOD            \
    CAP_NET_BIND_SERVICE \
    CAP_NET_RAW          \
    CAP_SETFCAP          \
    CAP_SETPCAP
ProcessTwo=yes
LinkJournal=no
Timezone=no

[Files]
Bind=/home/fossil/museum/myproject:/jail/museum

[Network]
VirtualEthernet=no
```

----

If you recognize most of that from the `Dockerfile` discussion above,
congratulations, you’ve been paying attention. The rest should also
be clear from context.

Some of this is expected to vary:

*   The references to `example.com` and `myproject` are stand-ins for
    your actual web site and repository name.

*   The command given in the `Parameters` directive assumes you’re
    setting up [SCGI proxying via nginx][DNT], but with adjustment,
    it’ll work with the other repository service methods we’ve
    [documented][srv].

*   The path in the host-side part of the `Bind` value must point at the
    directory containing the `repo.fossil` file referenced in said
    command so that `/jail/museum/repo.fossil` refers to your repo out
    on the host for the reasons given [above](#bind-mount).

That being done, we also need a generic systemd unit file called
`/etc/systemd/system/fossil@.service`, containing:

----

```
[Unit]
Description=Fossil %i Repo Service
Wants=modprobe@tun.service modprobe@loop.service
After=network.target systemd-resolved.service modprobe@tun.service modprobe@loop.service

[Service]
ExecStart=systemd-nspawn --settings=override --read-only --machine=%i bin/fossil

[Install]
WantedBy=multi-user.target
```

----

You shouldn’t have to change any of this because we’ve given the
`--setting=override` flag, meaning any setting in the nspawn file
overrides the setting passed to `systemd-nspawn`.  This arrangement
not only keeps the unit file simple, it allows multiple services to
share the base configuration, varying on a per-repo level through
adjustments to their individual `*.nspawn` files.

You may then start the service in the normal way:

```
  $ sudo systemctl enable fossil@myproject
  $ sudo systemctl start  fossil@myproject
```

You should then find it running on localhost port 9000 per the nspawn
configuration file above, suitable for proxying Fossil out to the
public using nginx via SCGI. If you aren’t using a front-end proxy
and want Fossil exposed to the world via HTTPS, you might say this instead in
the `*.nspawn` file:

```
Parameters=bin/fossil server \
    --cert /path/to/cert.pem \
    --chroot /jail           \
    --create                 \
    --jsmode bundled         \
    --port 443               \
    --user admin             \
    museum/repo.fossil
```

You would also need to un-drop the `CAP_NET_BIND_SERVICE` capability
to allow Fossil to bind to this low-numbered port.

We use systemd’s template file feature to allow multiple Fossil
servers running on a single machine, each on a different TCP port,
as when proxying them out as subdirectories of a larger site.
To add another project, you must first clone the base “machine” layer:

```
  $ sudo machinectl clone myproject otherthing
```

That will not only create a clone of `/var/lib/machines/myproject`
as `../otherthing`, it will create a matching `otherthing.nspawn` file for you
as a copy of the first one.  Adjust its contents to suit, then enable
and start it as above.

[mcfad]: https://www.freedesktop.org/software/systemd/man/machinectl.html#Files%20and%20Directories


### 6.3.1 <a id="nspawn-rhel"></a>Getting It Working on a RHEL Clone

The biggest difference between doing this on OSes like CentOS versus
Ubuntu is that RHEL (thus also its clones) doesn’t ship btrfs in
its kernel, thus ships with no package repositories containing `mkfs.btrfs`, which
[`machinectl`][mctl] depends on for achieving its various purposes.

Fortunately, there are workarounds.

First, the `apt install` command above becomes:

```
  $ sudo dnf install systemd-container
```

Second, you have to hack around the lack of `machinectl import-tar`:

```
  $ rootfs=/var/lib/machines/fossil
  $ sudo mkdir -p $rootfs
  $ docker container export fossil | sudo tar -xf -C $rootfs -
```

The parent directory path in the `rootfs` variable is important,
because although we aren’t able to use `machinectl` on such systems, the
`systemd-nspawn` developers assume you’re using them together; when you give
`--machine`, it assumes the `machinectl` directory scheme.  You could
instead use `--directory`, allowing you to store the rootfs wherever
you like, but why make things difficult?  It’s a perfectly sensible
default, consistent with the [LHS] rules.

The final element &mdash; the machine name &mdash; can be anything
you like so long as it matches the nspawn file’s base name.

Finally, since you can’t use `machinectl clone`, you have to make
a wasteful copy of `/var/lib/machines/myproject` when standing up
multiple Fossil repo services on a single machine.  (This is one
of the reasons `machinectl` depends on `btrfs`: cheap copy-on-write
subvolumes.)  Because we give the `--read-only` flag, you can simply
`cp -r` one machine to a new name rather than go through the
export-and-import dance you used to create the first one.

[LHS]:  https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html
[mctl]: https://www.freedesktop.org/software/systemd/man/machinectl.html


### 6.3.2 <a id="nspawn-weaknesses"></a>What Am I Missing Out On?

For all the runtime size savings in this method, you may be wondering
what you’re missing out on relative to Podman, which takes up
roughly 27× more disk space.  Short answer: lots.  Long answer:

1.  **Build system.**  You’ll have to build and test your containers
    some other way.  This method is only suitable for running them
    once they’re built.

2.  **Orchestration.**  All of the higher-level things like
    “compose” files, Docker Swarm mode, and Kubernetes are
    unavailable to you at this level.  You can run multiple
    instances of Fossil, but on a single machine only and with a
    static configuration.

3.  **Image layer sharing.**  When you update an image using one of the
    above methods, Docker and Podman are smart enough to copy only
    changed layers.  Furthermore, when you base multiple containers
    on a single image, they don’t make copies of the base layers;
    they can share them, because base layers are immutable, thus
    cannot cross-contaminate.

    Because we use `systemd-nspawn --read-only`, we get *some*
    of this benefit, particularly when using `machinectl` with
    `/var/lib/machines` as a btrfs volume.  Even so, the disk space
    and network I/O optimizations go deeper in the Docker and Podman
    worlds.

4.  **Tooling.** Hand-creating and modifying those `systemd`
    files sucks compared to “`podman container create ...`”  This
    is but one of many affordances you will find in the runtimes
    aimed at daily-use devops warriors.

5.  **Network virtualization.** In the scheme above, we turn off the
    `systemd` private networking support because in its default mode, it
    wants to hide containerized services entirely. While there are
    [ways][ndcmp] to expose Fossil’s single network service port under
    that scheme, it adds a lot of administration complexity. In the
    big-boy container runtimes, `docker create --publish` fixes all this
    up in a single option, whereas `systemd-nspawn --port` does
    approximately *none* of that despite the command’s superficial
    similarity.

    From a purely functional point of view, this isn’t a huge problem if
    you consider the inbound service direction only, being external
    connections to the Fossil service we’re providing. Since we do want
    this Fossil service to be exposed — else why are we running it? — we
    get all the control we need via `fossil server --localhost` and
    similar options.

    The complexity of the `systemd` networking infrastructure’s
    interactions with containers make more sense when you consider the
    outbound path.  Consider what happens if you enable Fossil’s
    optional TH1 docs feature plus its Tcl evaluation feature. That
    would enable anyone with the rights to commit to your repository the
    ability to make arbitrary network connections on the Fossil host.
    Then, let us say you have a client-server DBMS server on that same
    host, bound to localhost for private use by other services on the
    machine. Now that DBMS is open to access by a rogue Fossil committer
    because the host’s loopback interface is mapped directly into the
    container’s network namespace.

    Proper network virtualization would protect you in this instance.

This author expects that the set of considerations is broader than
presented here, but that it suffices to make our case as it is: if you
can afford the space of Podman or Docker, we strongly recommend using
either of them over the much lower-level `systemd-container`
infrastructure. You’re getting a considerable amount of value for the
higher runtime cost; it isn’t pointless overhead.

(Incidentally, these are essentially the same reasons why we no longer
talk about the `crun` tool underpinning Podman in this document. It’s
even more limited than `nspawn`, making it even more difficult to administer while
providing no runtime size advantage. The `runc` tool underpinning
Docker is even worse on this score, being scarcely easier to use than
`crun` while having a much larger footprint.)

[ndcmp]:  https://wiki.archlinux.org/title/systemd-networkd#Usage_with_containers


### 6.3.3 <a id="nspawn-assumptions"></a>Violated Assumptions

The `systemd-container` infrastructure has a bunch of hard-coded
assumptions baked into it.  We papered over these problems above,
but if you’re using these tools for other purposes on the machine
you’re serving Fossil from, you may need to know which assumptions
our container violates and the resulting consequences.

Some of it we discussed above already, but there’s one big class of
problems we haven’t covered yet. It stems from the fact that our stock
container starts a single static executable inside a barebones container
rather than “boot” an OS image. That causes a bunch of commands to fail:

*   **`machinectl poweroff`** will fail because the container
    isn’t running dbus.

*   **`machinectl start`** will try to find an `/sbin/init`
    program in the rootfs, which we haven’t got.  We could
    rename `/jail/bin/fossil` to `/sbin/init` and then hack
    the chroot scheme to match, but ick.  (This, incidentally,
    is why we set `ProcessTwo=yes` above even though Fossil is
    perfectly capable of running as PID 1, a fact we depend on
    in the other methods above.)

*   **`machinectl shell`** will fail because there is no login
    daemon running, which we purposefully avoided adding by
    creating a “`FROM scratch`” container. (If you need a
    shell, say: `sudo systemd-nspawn --machine=myproject /bin/sh`)

*   **`machinectl status`** won’t give you the container logs
    because we disabled the shared journal, which was in turn
    necessary because we don’t run `systemd` *inside* the
    container, just outside.

If these are problems for you, you may wish to build a
fatter container using `debootstrap` or similar. ([External
tutorial][medtut].)

[medtut]: https://medium.com/@huljar/setting-up-containers-with-systemd-nspawn-b719cff0fb8d

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
