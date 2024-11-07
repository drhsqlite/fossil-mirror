# Forcing Use of Fossil’s RBAC over SSH

Andy Bradford posted a [clever solution][sshfc] to the problem of
Fossil’s RBAC system [being ignored](../../caps/#webonly) over `ssh://`
URLs: use OpenSSH’s `ForceCommand` feature to route the sync transfer
protocol data over `fossil http` rather than `fossil test-http`.

The setup for this is complicated, but it’s a worthy option when you
need encrypted communications between the client and server, you already
have SSH set up, and [the HTTPS alternative](../../ssl.wiki) is
unworkable for some reason.


## 1. Force remote Fossil access through a wrapper script <a id="sshd"></a>

Put something like the following into the `sshd_config` file on the
Fossil repository server:

``` ssh-config
Match Group fossil
    X11Forwarding no
    AllowTcpForwarding no
    AllowAgentForwarding no
    ForceCommand /home/fossil/bin/wrapper
```

This file is usually found in `/etc/ssh`, but some OSes put it
elsewhere.

The first line presumes that we will put all users who need to use our
Fossil repositories into the `fossil` group, as we will do
[below](#perms). You could instead say something like:

``` ssh-config
Match User alice,bob,carol,dave
```

You have to list the users allowed to use Fossil in this case because
your system likely has a system administrator that uses SSH for remote
shell access, so you want to *exclude* that user from the list. For the
same reason, you don’t want to put the `ForceCommand` directive outside
a `Match` block of some sort.

You could instead list the exceptions:

``` ssh-config
Match User !evi
```

This would permit only [Evi the System Administrator][evi] to bypass this
mechanism.

[evi]: https://en.wikipedia.org/wiki/Evi_Nemeth

If you have a user that needs both interactive SSH shell access *and*
Fossil access, exclude that user from the `Match` rule and use Fossil’s
normal `ssh://` URL scheme for those cases. This user will bypass the
Fossil RBAC, but they effectively have Setup capability on those
repositories anyway by having full read/write access to the DB files via
the shell.


## 2. Rewrite the sync command with that wrapper <a id="wrapper"></a>

When Fossil syncs over SSH, it attempts to launch a remote Fossil
instance with certain parameters in order to set up the HTTP-based sync
protocol over that SSH tunnel. We need to preserve some of this command
and rewrite other parts to make this work.

Here is a simpler variant of Andy’s original wrapper script:

``` sh
#!/bin/bash
set -- $SSH_ORIGINAL_COMMAND
while [ $# -gt 1 ] ; do shift ; done
export REMOTE_USER="$USER"
ROOT=/home/fossil
exec "$ROOT/bin/fossil" http "$ROOT/museum/$(/bin/basename "$1")"
```

The substantive changes are:

1.  Move the command rewriting bits to the start.

2.  Be explicit about executable paths.  You might extend this idea by
    using chroot, BSD jails, Linux containers, etc.

3.  Restrict the Fossil repositories to a single flat subdirectory under
    the `fossil` user’s home directory. This scheme is easier to secure
    than one allowing subdirectories, since you’d need to take care of
    `../` and such to prevent a sandbox escape.

4.  Don’t take the user name via the SSH command; to this author’s mind,
    the user should not get to override their Fossil user name on the
    remote server, as that permits impersonation.  The identity you
    present to the SSH server must be the same identity that the Fossil
    repository you’re working with knows you by.  Since the users
    selected by “`Match`” block above are dedicated to using only Fossil
    in this setup, this restriction shouldn’t present a practical problem.

The script’s shebang line assumes `/bin/sh` is POSIX-compliant, but that
is not the case everywhere. If the script fails to run on your system,
try changing this line to point at `bash`, `dash`, `ksh`, or `zsh`. Also
check the absolute paths for local correctness: is `/bin/basename`
installed on your system, for example?

Under this scheme, you clone with a command like:

    $ fossil clone ssh://HOST/repo.fossil

This will clone the remote `/home/fossil/museum/repo.fossil` repository
to your local machine under the same name and open it into a “`repo/`”
subdirectory. Notice that we didn’t have to give the `museum/` part of
the path: it’s implicit per point #3 above.

This presumes your local user name matches the remote user name.  Unlike
with `http[s]://` URLs, you don’t have to provide the `USER@` part to
get authenticated access
since this scheme doesn’t permit anonymous cloning. Only
if these two user names are different do you need to add the `USER@` bit to the
URL.


## 3. Set permissions <a id="perms"></a>

This scheme assumes that the users covered by the `Match` rule can read
the wrapper script from where you placed it and execute it, and that
they have read/write access on the directory where the Fossil
repositories are stored.

You can achieve all of this on a Linux box with:

``` shell
sudo adduser fossil
for u in alice bob carol dave ; do 
    sudo adduser $u
    sudo gpasswd -a fossil $u
done
sudo -i -u fossil
chmod 710 .
mkdir -m 750 bin
mkdir -m 770 museum
ln -s /usr/local/bin/fossil bin
```

You then need to copy the Fossil repositories into `~fossil/museum` and
make them readable and writable by group `fossil`. These repositories
presumably already have Fossil users configured, with the necessary
[user capabilities](../../caps/), the point of this article being to
show you how to make Fossil-over-SSH pay attention to those caps.

You must also permit use of `REMOTE_USER` on each shared repository.
Fossil only pays attention to this environment variable in certain
contexts, of which “`fossil http`” is not one. Run this command against
each repo to allow that:

``` shell
echo "INSERT OR REPLACE INTO config VALUES ('remote_user_ok',1,strftime('%s','now'));" |
fossil sql -R museum/repo.fossil
```

Now you can configure SSH authentication for each user. Since Fossil’s
password-saving feature doesn’t work in this case, I suggest setting up
SSH keys via `~USER/.ssh/authorized_keys` since the SSH authentication
occurs on each sync, which Fossil’s default-enabled autosync setting
makes frequent.

Equivalent commands for other OSes should be readily discerned from the
script above.

[sshfc]: forum:/forumpost/0d7d6c3df41fcdfd

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
