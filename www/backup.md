# Backing Up a Remote Fossil Repository

One of the great benefits of Fossil and other [distributed version control systems][dvcs]
is that cloning a repository makes a backup. If you are running a project with multiple
developers who share their work using a [central server][server] and the server hardware
catches fire or otherwise becomes unavailable, the clones of the repository on each developer
workstation *may* serve as a suitable backup.

[dvcs]: wikipedia:/wiki/Distributed_version_control
[server]: ./server/whyuseaserver.wiki

We say “may” because
it turns out not everything in a Fossil repository is copied when cloning. You
don’t even always get copies of all historical file artifacts. More than
that, a Fossil repository typically contains
other useful information that is not always shared as part of a clone, which might need
to be backed up separately.  To wit:


## Sensitive Information

Fossil purposefully does not clone certain sensitive information unless
you’re logged in as a user with [setup] capability. As an example, a local clone
may have a different `user` table than the remote, because only a
Setup user is allowed to see the full version for privacy and security
reasons.


## Configuration Drift

Fossil allows the local configuration in certain areas to differ from
that of the remote. With the exception of the prior item, you get a copy
of these configuration areas on initial clone, but after that, some
remote configuration changes don’t sync down automatically, such as the
remote’s skin. You can ask for updates by running the
[`fossil config pull skin`](./help?cmd=config) command, but that
does not happen automatically during the course of normal development.


## Private Branches

The very nature of Fossil’s [private branch feature][pbr] ensures that
remote clones don’t get a copy of those branches. Normally this is
exactly what you want, but in the case of making backups, you probably
want to back up these branches as well. One of the two backup methods below
provides this.


## Shunned Artifacts

Fossil purposefully doesn’t sync [shunned artifacts][shun]. If you want
your local clone to be a precise match to the remote, it needs to track
changes to the shun table as well.


## Unversioned Artifacts

Data in Fossil’s [unversioned artifacts table][uv] doesn’t sync down by
default unless you specifically ask for it. Like local configuration
data, it doesn’t get pulled as part of a normal `fossil sync`, but
*unlike* the config data, you don’t get unversioned files as part of the
initial clone unless you ask for it by passing the `--unversioned/-u`
flag.


## Autosync Is Intransitive

If you’re using Fossil in a truly distributed mode, rather than the
simple central-and-clones model that is more common, there may be no
single source of truth in the network because Fossil’s autosync feature
isn’t transitive.

That is, if you cloned from server A, and then you stand that up on a
server B, then if I clone from your server as my repository C, your changes to B
autosync up to A, but not down to me on C until I do something locally
that triggers autosync. The inverse is also true: if I commit something
on C, it will autosync up to B, but A won’t get a copy until someone on
B does something to trigger a sync there.

An easy way to run into this problem is to set up failover servers
`svr1` thru `svr3.example.com`, then set `svr2` and `svr3` up to sync
with the first.  If all of the users normally clone from `svr1`, their
commits don’t get to `svr2` and `svr3` until something on one of the
servers pushes or pulls the changes down to the next server in the sync
chain.

Likewise, if `svr1` falls over and all of the users re-point their local
clones at `svr2`, then `svr1` later reappears, `svr1` is likely to
remain a stale copy of the old version of the repository until someone
causes it to sync with `svr2` or `svr3` to catch up again.  And then if
you originally designed the sync scheme to treat `svr1` as the primary
source of truth, those users still syncing with `svr2` won’t have their
commits pushed up to `svr1` unless you’ve set up bidirectional sync,
rather than have the two backup servers do `pull` only.


# Solutions

The following script solves most of the above problems for the use case
where you want a *nearly-complete* clone of the remote repository using nothing
but the normal Fossil sync protocol. It only does so if you are logged into
the remote as a user with Setup capability, however.

----

``` shell
#!/bin/sh
fossil sync --unversioned
fossil configuration pull all
fossil rebuild
```

----

The last step is needed to ensure that shunned artifacts on the remote
are removed from the local clone. The second step includes
`fossil conf pull shun`, but until those artifacts are actually rebuilt
out of existence, your backup will be “more than complete” in the sense
that it will continue to have information that the remote says should
not exist any more. That would be not so much a “backup” as an
“archive,” which might not be what you want.

This method doesn’t get you a copy of the remote’s
[private branches][pbr], on purpose. It may also miss other info on the
remote, such as SQL-level customizations that the sync protocol can’t
see. (Some [ticket system customization][tkt] schemes rely on this ability, for example.) You can
solve such problems if you have access to the remote server, which
allows you to get a SQL-level backup. This requires Fossil 2.12 or
newer, which added [the `backup` command][bu] to take care of
locking and transaction isolation, allowing the user to safely back up an in-use
repository.

If you have SSH access to the remote server, something like this will work:

----

``` shell
#!/bin/bash
bf=repo-$(date +%Y-%m-%d).fossil
ssh example.com "cd museum ; fossil backup -R repo.fossil backups/$bf" &&
    scp example.com:museum/backups/$bf ~/museum/backups
```


## Encrypted Off-Site Backups

A useful refinement that you can apply to both methods above is
encrypted off-site backups. You may wish to store backups of your
repositories off-site on a service such as Dropbox, Google Drive, iCloud,
or Microsoft OneDrive, where you don’t fully trust the service not to
leak your information. This addition to the prior scripts will encrypt
the resulting backup in such a way that the cloud copy is a useless blob
of noise to anyone without the key:

----

```shell
pass="h8TixP6Mt6edJ3d6COaexiiFlvAM54auF2AjT7ZYYn"
gd="$HOME/Google Drive/Fossil Backups/$bf.xz.enc"
fossil sql -R ~/museum/backups/"$bf" .dump | xz -9 |
    openssl enc -e -aes-256-cbc -pbkdf2 -iter 52830 -pass pass:"$pass" -out "$gd"
```

----

If you’re adding this to the first script above, remove the
“`-R repo-name`” bit so you get a dump of the repository backing the
current working directory.

This requires OpenSSL 1.1 or higher. If you’re on 1.0 or older, you
won’t have the `-pbkdf2` and `-iter` options, and you may have to choose
a different cipher algorithm; both changes are likely to weaken the
encryption significantly, so you should install a newer version rather
than work around the lack of these features.

If you’re on macOS, which
still ships 1.0 as of the time of this writing, [Homebrew][hb] offers
the current version of OpenSSL, but to avoid a conflict with the platform
version it’s [unlinked][hbul] by default, so you have to give an explicit
path to its “cellar” directory:

       /usr/local/Cellar/openssl\@1.1/1.1.1g/bin/openssl ...

Change the `pass` value to some other long random string, and change the
`iter` value to something between 10000 and 100000. A good source for
the first is [here][grcp], and for the second, [here][rint].

Compressing the data before encrypting it removes redundancies that can
make decryption easier, and it results in a smaller backup than you get
with the previous script alone, at the expense of a lot of CPU time
during the backup. You may wish to switch to a less space-efficient
compression algorithm that takes less CPU power, such as [`lz4`][lz4].
Changing up the compression algorithm also provides some
security-thru-obscurity, which is useless on its own, but it *is* a
useful adjunct to strong encryption.


## Restoring From An Encrypted Backup

The “restore” script for the above fragment is basically an inverse of
it, but it’s worth showing it because there are some subtleties to take
care of. If all variables defined in earlier scripts are available, then
restoration is:

```
openssl enc -d -aes-256-cbc -pbkdf2 -iter 52830 -pass pass:"$pass" -in "$gd" |
    xz -d | sqlite3 ~/museum/restored-repo.fossil
```

We changed the `-e` to `-d` on the `openssl` command to get decryption,
and we changed the `-out` to `-in` so it reads from the encrypted backup
file and writes the result to stdout.

The decompression step is trivial.

The last change is tricky: we used `fossil sql` above to ensure that
we’re using the same version of SQLite to write the encrypted backup DB
as was used to maintain the repository, but unfortunately, we can’t get
the built-in SQLite shell to write a backup into an empty database.
(As soon as it starts up, it goes looking for tables created by
`fossil init` and fails with an error.)
Therefore, we have to either run the restoration against a
possibly-different version of SQLite and hope there are no
incompatibilities, or we have to go out of our way to build a matching
version of `sqlite3` before we can safely do the restoration.

Keep in mind that Fossil often acts as a dogfooding project for SQLite,
making use of the latest features, so it is quite likely that a given
random `sqlite3` binary in your `PATH` will be unable to understand the
file created by “`fossil sql .dump`”!

[bu]:    /help?cmd=backup
[grcp]:  https://www.grc.com/passwords.htm
[hb]:    https://brew.sh
[hbul]:  https://docs.brew.sh/FAQ#what-does-keg-only-mean
[lz4]:   https://lz4.github.io/lz4/
[pbr]:   ./private.wiki
[rint]:  https://www.random.org/integers/?num=1&min=10000&max=100000&col=5&base=10&format=html&rnd=new
[setup]: ./caps/admin-v-setup.md#apsu
[shun]:  ./shunning.wiki
[tkt]:   ./tickets.wiki
[uv]:    ./unvers.wiki
