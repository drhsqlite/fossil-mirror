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
add APK packages to support those options, and so forth. It also strips
out all but the default and darkmode skins to save executable space.

The Fossil `Makefile` provides two convenience targets,
“`make container-image`” and “`make container-run`”. The first creates a
versioned container image, and the second does that and then launches a
fresh container based on that image. You can pass extra arguments to the
first command via the Makefile’s `DBFLAGS` variable and to the second
with the `DRFLAGS` variable. (DB is short for “`docker build`”, and DR
is short for “`docker run`”.) To get the custom port setting as in
second command above, say:

```
  $ make container-run DRFLAGS='-p 9999:8080/tcp'
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
    --name fossil-bind-mount -p 9999:8080 \
    -v ~/museum:/jail/museum fossil
```

Because this bind mount maps a host-side directory (`~/museum`) into the
container, you don’t need to `docker cp` the repo into the container at
all. It still expects to find the repository as `repo.fossil` under that
directory, but now both the host and the container can see that file.
(Beware: This may create a [risk of data corruption][dbcorr] due to
SQLite locking issues if you try to modify the DB from both sides at
once.)

Instead of a bind mount, you could instead set up a separate [Docker
volume](https://docs.docker.com/storage/volumes/), at which point you
_would_ need to `docker cp` the repo file into the container.

Either way, files in these mounted directories have a lifetime
independent of the container(s) they’re mounted into. When you need to
rebuild the container or its underlying image — such as to upgrade to a
newer version of Fossil — the external directory remains behind and gets
remapped into the new container when you recreate it with `-v`.

[dbcorr]: https://www.sqlite.org/howtocorrupt.html


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
at about 4 MiB. (It’s built stripped and packed with [UPX].)

[UPX]: https://upx.github.io/


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
you’re running the build command from, based on checkin ID. The most
common reason to override this is to get a release version:

```
  $ docker build -t fossil \
    --build-arg FSLVER=version-2.19 .
```

It’s best to use a specific version number rather than the generic
“`release`” tag because Docker caches downloaded files and tries to
reuse them across builds. If you ask for “`release`” before a new
version is tagged and then immediately after, you might expect to get
two different tarballs, but because the URL hasn’t changed, if you have
an old release tarball in your Docker cache, you’ll get the old version
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
  $ docker build -t fossil --build-arg UID=501 .
```

This is particularly useful if you’re putting your repository on a
Docker volume since the IDs “leak” out into the host environment via
file permissions. You may therefore wish them to mean something on both
sides of the container barrier rather than have “499” appear on the host
in “`ls -l`” output.


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
don’t have to resort to [raw Fossil service](./server/) to succeed,
leaving the benefits of containerization to those with bigger budgets.

For the sake of simple examples in this section, we’ll assume you’re
integrating Fossil into a larger web site, such as with our [Debian +
nginx + TLS][DNT] plan. The Fossil server instance listens on a
high-numbered port, on localhost only, and the front-end web server
reverse-proxies this out to the public.  Containers are a fine addition
to such a system, isolating those elements of the site, thus greatly
reducing the chance that they’ll ever be used to break into the host as
a whole.

(If you wanted to be double-safe, you could put the web server into
another container, restricting it only to reading from the static web
site directory and connecting across localhost to back-end dynamic
content servers such as Fossil. That’s way outside the scope of this
document, but you can find ready advice for that elsewhere. Seeing how
we do this with Fossil should help you bridge the gap in extending
this idea to the rest of your site.)

[DD]:  https://www.docker.com/products/docker-desktop/
[DE]:  https://docs.docker.com/engine/
[DNT]: ./server/debian/nginx.md


### 6.1 <a id="runc" name="containerd"></a>Stripping Docker Engine Down

The core of Docker Engine is its [`containerd`][ctrd] daemon and the
[`runc`][runc] container runner. It’s possible to dig into the subtree
managed by `containerd` on the build host and extract what we need to
run our Fossil container elsewhere with `runc`, leaving out all the
rest. `runc` alone is about 18 MiB, and you can do without `containerd`
entirely, if you want.

The method isn’t complicated, but it *is* cryptic enough to want a shell
script:

----

```shell
#!/bin/sh
c=fossil
b=$HOME/containers/$c
r=$b/rootfs
m=/run/containerd/io.containerd.runtime.v2.task/moby

if [ -d "$t" ] && mkdir -p $r
then
    docker container start  $c
    docker container export $c | sudo tar -C $r -xf -
    id=$(docker inspect --format="{{.Id}}" $c)
    sudo cat $m/$id/config.json |
        jq '.root.path = "'$r'"' |
        jq '.linux.cgroupsPath = ""' |
        jq 'del(.linux.sysctl)' |
        jq 'del(.linux.namespaces[] | select(.type == "network"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/hostname"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/resolv.conf"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/hosts"))' |
        jq 'del(.hooks)' > $b/config.json
fi
```

----

The first several lines list configurables:

*   **`b`**: the path of the exported container, called the “bundle” in OCI
    jargon
*   **`c`**: the name of the Docker container you’re bundling up for use
    with `runc`
*   **`m`**: the directory holding the running machines, configurable
    because:
    *   it’s long
    *   it’s been known to change from one version of Docker to the next
    *   you might be using [Podman](#podman)/[`crun`](#crun), so it has
        to be “`/run/user/$UID/crun`” instead
*   **`r`**: the path of the directory containing the bundle’s root file
    system.

That last doesn’t have to be called `rootfs/`, and it doesn’t have to
live in the same directory as `config.json`, but it is conventional.
Because some OCI tools use those names as defaults, it’s best to follow
suit.

The rest is generic, but you’re welcome to freestyle here. We’ll show an
example of this below.

We’re using [jq] for two separate purposes:

1.  To automatically transmogrify Docker’s container configuration so it
    will work with `runc`:

    *   point it where we unpacked the container’s exported rootfs
    *   accede to its wish to [manage cgroups by itself][ecg]
    *   remove the `sysctl` calls that will break after…
    *   …we remove the network namespace to allow Fossil’s TCP listening
        port to be available on the host; `runc` doesn’t offer the
        equivalent of `docker create --publish`, and we can’t be
        bothered to set up a manual mapping from the host port into the
        container
    *   remove file bindings that point into the local runtime managed
        directories; one of the things we give up by using a bare
        container runner is automatic management of these files
    *   remove the hooks for essentially the same reason

2.  To make the Docker-managed machine-readable `config.json` more
    human-readable, in case there are other things you want changed in
    this version of the container.  Exposing the `config.json` file like
    this means you don’t have to rebuild the container merely to change
    a value like a mount point, the kernel capability set, and so forth.

<a id="why-sudo"></a>
We have to do this transformation of `config.json` as the local root
user because it isn’t readable by your normal user. Additionally, that
input file is only available while the container is started, which is
why we ensure that before exporting the container’s rootfs.

With the container exported like this, you can start it as:

```
  $ cd /path/to/bundle
  $ c=any-name-you-like
  $ sudo runc create $c
  $ sudo runc start  $c
  $ sudo runc exec $c -t sh -l
  ~ $ ls museum
  repo.fossil
  ~ $ ps -eaf
  PID   USER     TIME  COMMAND
      1 fossil    0:00 bin/fossil server --create …
  ~ $ exit
  $ sudo runc kill fossil-runc
  $ sudo runc delete fossil-runc
```

If you’re doing this on the export host, the first command is “`cd $b`”
if we’re using the variables from the shell script above. We do this
because `runc` assumes you’re running it from the bundle directory. If
you prefer, the `runc` commands that care about this take a
`--bundle/-b` flag to let you avoid switching directories.

The rest should be straightforward: create and start the container as
root so the `chroot(2)` call inside the container will succeed, then get
into it with a login shell and poke around to prove to ourselves that
everything is working properly. It is. Yay!

The remaining commands show shutting the container down and destroying
it, simply to show how these commands change relative to using the
Docker Engine commands. It’s “kill,” not “stop,” and it’s “delete,” not
“rm.”

If you want the bundle to run on a remote host, the local and remote
bundle directories likely will not match, as the shell script above
assumes.  This is a more realistic shell script for that case:

----

```shell
#!/bin/bash -ex
c=fossil
b=/var/lib/machines/$c
h=my-host.example.com
m=/run/containerd/io.containerd.runtime.v2.task/moby
t=$(mktemp -d /tmp/$c-bundle.XXXXXX)

if [ -d "$t" ]
then
    docker container start  $c
    docker container export $c > $t/rootfs.tar
    id=$(docker inspect --format="{{.Id}}" $c)
    sudo cat $m/$id/config.json |
        jq '.root.path = "'$b/rootfs'"' |
        jq '.linux.cgroupsPath = ""' |
        jq 'del(.linux.sysctl)' |
        jq 'del(.linux.namespaces[] | select(.type == "network"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/hostname"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/resolv.conf"))' |
        jq 'del(.mounts[] | select(.destination == "/etc/hosts"))' |
        jq 'del(.hooks)' > $t/config.json
    scp -r $t $h:tmp
        ssh -t $h "{
                mv ./$t/config.json $b &&
                sudo tar -C $b/rootfs -xf ./$t/rootfs.tar &&
                rm -r ./$t
        }"
    rm -r $t
fi
```

----

We’ve introduced two new variables:

*   **`h`**: the remote host name
*   **`t`**: a temporary bundle directory we populate locally, then
    `scp` to the remote machine, where it’s unpacked

We dropped the **`r`** variable because now we have two different
“rootfs” types: the tarball and the unpacked version of that tarball.
To avoid confusing ourselves between these cases, we’ve replaced uses of
`$r` with explicit paths.

You need to be aware that this script uses `sudo` for two different purposes:

1. To read the local `config.json` file out of the `containerd` managed
   directory. ([Details above](#why-sudo).)

2. To unpack the bundle onto the remote machine. If you try to get
   clever and unpack it locally, then `rsync` it to the remote host to
   avoid re-copying files that haven’t changed since the last update,
   you’ll find that it fails when it tries to copy device nodes, to
   create files owned only by the remote root user, and so forth. If the
   container bundle is small, it’s simpler to re-copy and unpack it
   fresh each time.

I point that out because it might ask for your password twice: once for
the local sudo command, and once for the remote.

The default for the **`b`** variable is the convention for systemd based
machines, which will play into the [`nspawn` alternative below][sdnsp].
Even if you aren’t using `nspawn`, it’s a reasonable place to put
containers under the [Linux FHS rules][LFHS].

[ctrd]:  https://containerd.io/
[ecg]:   https://github.com/opencontainers/runc/pull/3131
[LFHS]:  https://en.wikipedia.org/wiki/Filesystem_Hierarchy_Standard
[jq]:    https://stedolan.github.io/jq/
[sdnsp]: #nspawn
[runc]:  https://github.com/opencontainers/runc


### 6.2 <a id="podman"></a>Podman

Although your humble author claims the `runc` methods above are not
complicated, merely cryptic, you might be fondly recollecting the
carefree commands at the top of this document, pondering whether you can
live without the abstractions a proper container runtime system
provides.

More than that, there’s a hidden cost to the `runc` method: there is no
layer sharing among containers. If you have multiple Fossil containers
on a single host — perhaps because each serves an independent section of
the overall web site — and you export them to a remote host using the
shell script above, you’ll end up with redundant copies of the `rootfs`
in each. A proper OCI container runtime knows they’re all derived from
the same base image, differing only in minor configuration details,
giving us one of the major advantages of containerization: if none of
the running containers can change these immutable base layers, it
doesn’t have to copy them.

A lighter-weight alternative to Docker Engine that doesn’t give up so
many of its administrator affordances is [Podman], initially created by
Red Hat and thus popular on that family of OSes, although it will run on
any flavor of Linux. It can even be made to run [on macOS via Homebrew][pmmac]
or [on Windows via WSL2][pmwin].

On Ubuntu 22.04, it’s about a quarter the size of Docker Engine. That
isn’t nearly so slim as `runc`, but we may be willing to pay this
overhead to get shorter and fewer commands.

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
  $ make reconfig      # re-generate Dockerfile from the changed .in file
  $ docker build -t fossil:nojail .
  $ docker create \
    --name fossil-nojail \
    --publish 9999:8080 \
    --volume ~/museum/my-project.fossil:/museum/repo.fossil \
    fossil:nojail
```

This shows a new trick: mapping a single file into the container, rather
than mapping a whole directory. That’s only suitable if you aren’t using
WAL mode on that repository, or you aren’t going to use that repository
outside the container. It isn’t yet clear to me if WAL can work safely
across the container boundary, so for now, I advise that you either do
not use WAL mode with these containers, or that you clone the repository
locally for use outside the container and rely on Fossil’s autosync
feature to keep the two copies synchronized.

Do realize that by doing this, if an attacker ever managed to get shell
access on your container, they’d have a BusyBox installation to play
around in. That shouldn’t be enough to let them break out of the
container entirely, but they’ll have powerful tools like `wget`, and
they’ll be connected to the network the container runs on. Once the bad
guy is inside the house, he doesn’t necessarily have to go after the
residents directly to cause problems for them.


#### 6.2.2 <a id="crun"></a>`crun`

In the same way that [Docker Engine is based on `runc`](#runc), Podman’s
engine is based on [`crun`][crun], a lighter-weight alternative to
`runc`. It’s only 1.4 MiB on the system I tested it on, yet it will run
the same container bundles as in my `runc` examples above.
Above, we saved more than that by compressing the container’s Fossil
executable with UPX!

This makes `crun` a great option for tiny remote hosts with a single
container, or at least where none of the containers share base layers,
so that there is no effective cost to duplicating the immutable base
layers of the containers’ source images.

This suggests one method around the problem of rootless Podman containers:
`sudo crun`, following the examples above.

[crun]:   https://github.com/containers/crun


#### 6.2.3 <a id="podman-rootful"></a>Fossil in a Rootful Podman Container

##### Simple Method

As we saw above with `runc`, switching to `crun` just to get your
containers to run as root loses a lot of functionality and requires a
bunch of cryptic commands to get the same effect as a single command
under Podman.

Fortunately, it’s easy enough to have it both ways. Simply run your
`podman` commands as root:

```
  $ sudo podman build -t fossil --cap-add MKNOD .
  $ sudo podman create \
    --name fossil \
    --cap-drop AUDIT_WRITE \
    --cap-drop CHOWN \
    --cap-drop FSETID \
    --cap-drop KILL \
    --cap-drop NET_BIND_SERVICE \
    --cap-drop NET_RAW \
    --cap-drop SETFCAP \
    --cap-drop SETPCAP \
    --publish 9999:8080 \
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
into root’s Podman image repository, where the next steps look for it.

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
build.  Unlike Docker, Podman sensibly denies this by default, which
lets us leave off the corresponding `--cap-drop` option.


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

This round-trip through the public image repository has another side
benefit: your local system might be a lot faster than your remote one,
as when the remote is a small VPS. Even with the overhead of schlepping
container images across the Internet, it can be a net win in terms of
build time.



### 6.3 <a id="nspawn"></a>`systemd-nspawn`

As of `systemd` version 242, its optional `nspawn` piece
[reportedly](https://www.phoronix.com/news/Systemd-Nspawn-OCI-Runtime)
now has the ability to run OCI container bundles directly. You might
have it installed already, but if not, it’s only about 2 MiB.  It’s
in the `systemd-containers` package as of Ubuntu 22.04 LTS:

```
  $ sudo apt install systemd-containers
```

It’s also in CentOS Stream 9, under the same name.

You create the bundles the same way as with [the `runc` method
above](#runc). The only thing that changes are the top-level management
commands:

```
  $ sudo systemd-nspawn \
    --oci-bundle=/var/lib/machines/fossil \
    --machine=fossil \
    --network-veth \
    --port=9999:8080
  $ sudo machinectl list
  No machines.
```

This is why I wrote “reportedly” above: it doesn’t work on two different
Linux distributions, and I can’t see why. I’m putting this here to give
someone else a leg up, with the hope that they will work out what’s
needed to get the container running and registered with `machinectl`.

As of this writing, the tool expects an OCI container version of
“1.0.0”. I had to edit this at the top of my `config.json` file to get
the first command to read the bundle. The fact that it errored out when
I had “`1.0.2-dev`” in there proves it’s reading the file, but it
doesn’t seem able to make sense of what it finds there, and it doesn’t
give any diagnostics to say why.


<div style="height:50em" id="this-space-intentionally-left-blank"></div>
