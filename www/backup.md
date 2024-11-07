# Backing Up a Remote Fossil Repository

One of the great benefits of Fossil and other [distributed version control systems][dvcs]
is that cloning a repository makes a backup. If you are running a project with multiple
developers who share their work using a [central server][server] and the server hardware
catches fire, the clones of the repository on each developer
workstation *may* serve as a suitable backup.

[dvcs]: wikipedia:/wiki/Distributed_version_control
[server]: ./server/whyuseaserver.wiki

We say “may” because
it turns out not everything in a Fossil repository is copied when cloning. You
don’t even always get copies of all historical file artifacts. More than
that, a Fossil repository typically contains
other useful information that is not always shared as part of a clone, which might need
to be backed up separately.  To wit:


## <a id="pii"></a> Sensitive Information

Fossil purposefully does not clone certain sensitive information unless
you’re logged in as a user with [Setup] capability. As an example, a local clone
may have a different `user` table than the remote, because only a
Setup user is allowed to see the full version for privacy and security
reasons.


## <a id="config"></a> Configuration Drift

Fossil allows the local configuration to differ in several areas from
that of the remote. You get a copy
of *some* of these configuration areas on initial clone — not all! — but after that,
remote configuration changes mostly do not sync down automatically.


#### <a id="skin"></a> Skin

Changes to the remote’s skin don’t sync down, on purpose, since you may
want to have a different skin on the local clone than on the remote. You
can ask for updates with [`fossil config pull skin`][cfg], but that does
not happen automatically during the course of normal development.


#### <a id="alerts"></a> Email Alerts

The Admin → Notification settings do not get copied on clone or sync,
and it is not possible to push such settings from one repository to
another. We did this on purpose because you may have a network of peer
repositories, and you only want one repository sending email alerts. If
Fossil were to automatically replicate the email alert settings to a
separate repository, subscribers would get multiple alerts for each
event, which would be *bad.*

The only element of the email alert configuration that can be pulled
over the sync protocol on demand is the subscriber list, via
[`fossil config pull subscriber`][cfg].


#### <a id="project"></a> Project Configuration

This is normally generated once during `fossil init` and never changed,
so Fossil doesn’t pull this information without being forced, on
purpose. You could accidentally merge two separate Fossil repos by
pushing one repo’s project config up to another, for example.


#### <a id="other-cfg"></a> Others

A repo’s URL aliases, [interwiki configuration](./interwiki.md), and
[ticket customizations](./custom_tcket.wiki) also do not normally sync.

[cfg]: /help?cmd=configuration



## <a id="private"></a> Private Branches

The very nature of Fossil’s [private branch feature][pbr] ensures that
remote clones don’t get a copy of those branches. Normally this is
exactly what you want, but in the case of making backups, you probably
want to back up these branches as well. One of the two backup methods below
provides this.


## <a id="shun"></a> Shunned Artifacts

Fossil purposefully doesn’t sync [shunned artifacts][shun]. If you want
your local clone to be a precise match to the remote, it needs to track
changes to the shun table as well.


## <a id="uv"></a> Unversioned Artifacts

Data in Fossil’s [unversioned artifacts table][uv] doesn’t sync down by
default unless you specifically ask for it. Like local configuration
data, it doesn’t get pulled as part of a normal `fossil sync`, but
*unlike* the config data, you don’t get unversioned files as part of the
initial clone unless you ask for it by passing the `--unversioned/-u`
flag.


## <a id="ait"></a>Autosync Is Intransitive

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


# <a id="sync-solution"></a> Solution 1: Explicit Pulls

The following script solves most of the above problems for the use case
where you want a *nearly-complete* clone of the remote repository using nothing
but the normal Fossil sync protocol. It only does so if you are logged into
the remote as a user with Setup capability, however.

``` shell
#!/bin/sh
fossil sync --unversioned
fossil configuration pull all
fossil rebuild
```

The last step is needed to ensure that shunned artifacts on the remote
are removed from the local clone. The second step includes
`fossil conf pull shun`, but until those artifacts are actually rebuilt
out of existence, your backup will be “more than complete” in the sense
that it will continue to have information that the remote says should
not exist any more. That would be not so much a “backup” as an
“archive,” which might not be what you want.


# <a id="sql-solution"></a> Solution 2: SQL-Level Backup

The first method doesn’t get you a copy of the remote’s
[private branches][pbr], on purpose. It may also miss other info on the
remote, such as SQL-level customizations that the sync protocol can’t
see. (Some [ticket system customization][tkt] schemes rely on this ability, for example.) You can
solve such problems if you have access to the remote server, which
allows you to get a SQL-level backup by delegating handling of locking
and transaction isolation to
[the `backup` command][bu], allowing the user to safely back up an in-use
repository.

If you have SSH access to the remote server, something like this will work:

``` shell
#!/bin/bash
bf=repo-$(date +%Y-%m-%d).fossil
ssh example.com "cd museum ; fossil backup -R repo.fossil backups/$bf" &&
    scp example.com:museum/backups/$bf ~/museum/backups
```

Beware that this method does not solve [the intransitive sync
problem](#ait), in and of itself: if you do a SQL-level backup of a
stale repo DB, you have a *stale backup!* You should therefore run this
on every node that may need to serve as a backup so that at least *one*
of the backups is also up-to-date.


# <a id="enc"></a> Encrypted Off-Site Backups

A useful refinement that you can apply to both methods above is
encrypted off-site backups. You may wish to store backups of your
repositories off-site on a service such as Dropbox, Google Drive, iCloud,
or Microsoft OneDrive, where you don’t fully trust the service not to
leak your information. This addition to the prior scripts will encrypt
the resulting backup in such a way that the cloud copy is a useless blob
of noise to anyone without the key:

```shell
iter=152830
pass="h8TixP6Mt6edJ3d6COaexiiFlvAM54auF2AjT7ZYYn"
gd="$HOME/Google Drive/Fossil Backups/$bf.xz.enc"
fossil sql -R ~/museum/backups/"$bf" .dump | xz -9 |
    openssl enc -e -aes-256-cbc -pbkdf2 -iter $iter -pass pass:"$pass" -out "$gd"
```

If you’re adding this to the first script above, remove the
“`-R repo-name`” bit so you get a dump of the repository backing the
current working directory.

Change the `pass` value to some other long random string, and change the
`iter` value to something in the hundreds of thousands range. A good source for
the first is [here][grcp], and for the second, [here][rint].

You may find posts online written by people recommending millions of
iterations for PBKDF2, but they’re generally talking about this in the
context of memorizable passwords, where adding even one more character
to the password is a significant burden. Given our script’s purely
random maximum-length passphrase, there isn’t much more that increasing
the key derivation iteration count can do for us.

Conversely, if you were to reduce the passphrase to 41 characters, that
would drop the key strength by roughly 2⁶, being the entropy value per
character for using most of printable ASCII in our passphrase. To make
that lost strength up on the PBKDF2 end, you’d have to multiply your
iterations by 2⁶ = 64 times. It’s easier to use a max-length passphrase
in this situation than get crazy with key derivation iteration counts.

(This, by the way, is why the example passphrase above is 42 characters:
with 6 bits of entropy per character, that gives you a key size of 252,
as close as we can get to our chosen encryption algorithm’s 256-bit key
size without going over. If it pleases you to give it 43 random
characters for a passphrase in order to pick up those last four bits of
security, you’re welcome to do so.)

Compressing the data before encrypting it removes redundancies that can
make decryption easier, and it results in a smaller backup than you get
with the previous script alone, at the expense of a lot of CPU time
during the backup. You may wish to switch to a less space-efficient
compression algorithm that takes less CPU power, such as [`lz4`][lz4].
Changing up the compression algorithm also provides some
security-thru-obscurity, which is useless on its own, but it *is* a
useful adjunct to strong encryption.

This requires OpenSSL 1.1 or higher. If you’re on 1.0 or older, you
won’t have the `-pbkdf2` and `-iter` options, and you may have to choose
a different cipher algorithm; both changes are likely to weaken the
encryption significantly, so you should install a newer version rather
than work around the lack of these features.

Beware that macOS ships a fork of OpenSSL called [LibreSSL][lssl] that
lacked this capability until Ventura (13.0). If you’re on Monterey (12)
or older, we recommend use of the [Homebrew][hb] OpenSSL package rather
than give up on the security afforded by use of configurable-iteration
PBKDF2. To avoid a conflict with the platform’s `openssl` binary,
Homebrew’s installation is [unlinked][hbul] by default, so you have to
give an explicit path to it, one of:

    /usr/local/opt/openssl/bin/openssl ...     # Intel x86 Macs
    /opt/homebrew/opt/openssl/bin/openssl ...  # ARM Macs (“Apple silicon”)

[lssl]: https://www.libressl.org/


## <a id="rest"></a> Restoring From An Encrypted Backup

The “restore” script for the above fragment is basically an inverse of
it, but it’s worth showing it because there are some subtleties to take
care of. If all variables defined in earlier scripts are available, then
restoration is:

```
openssl enc -d -aes-256-cbc -pbkdf2 -iter $iter -pass pass:"$pass" -in "$gd" |
    xz -d | fossil sql --no-repository ~/museum/restored-repo.fossil
```

We changed the `-e` to `-d` on the `openssl` command to get decryption,
and we changed the `-out` to `-in` so it reads from the encrypted backup
file and writes the result to stdout.

The decompression step is trivial.

The last change is tricky: we used `fossil sql` above to ensure that
we’re using the same version of SQLite to write the encrypted backup DB
as was used to maintain the repository. We must also do that on
restoration:
Fossil serves as a dogfooding project for SQLite,
often making use of the latest features, so it is quite likely that a given
random `sqlite3` binary in your `PATH` will be unable to understand the
file created by “`fossil sql .dump`”! The tricky bit is, you can’t just
pipe the decrypted SQL dump into `fossil sql`, because on startup, Fossil
normally goes looking for tables created by `fossil init`, and it won’t
find them in a newly-created repo DB. We get around this by passing
the `--no-repository` flag, which suppresses this behavior. Doing it
this way saves you from needing to go and build a matching version of
`sqlite3` just to restore the backup.

[bu]:    /help?cmd=backup
[grcp]:  https://www.grc.com/passwords.htm
[hb]:    https://brew.sh
[hbul]:  https://docs.brew.sh/FAQ#what-does-keg-only-mean
[lz4]:   https://lz4.github.io/lz4/
[pbr]:   ./private.wiki
[rint]:  https://www.random.org/integers/?num=1&min=100000&max=1000000&col=5&base=10&format=html&rnd=new
[Setup]: ./caps/admin-v-setup.md#apsu
[shun]:  ./shunning.wiki
[tkt]:   ./tickets.wiki
[uv]:    ./unvers.wiki
