# Backing Up a Remote Fossil Repository

Simply cloning a Fossil repository does not necessarily create a
*complete* backup of the remote repository’s contents. With an existing
clone, Fossil’s autosync feature isn’t enough to keep that clone fully
up-to-date in a backup failover sense. This document explains what your
clone may be missing and how to ensure that it is complete for cases
where you intend to provide a complete backup of the remote.



## Sensitive Information

Fossil purposefully does not clone certain sensitive information unless
you’re logged in with [setup] capability. As an example, a local clone
may have a different `user` table than the remote, because only the
Setup user is allowed to see the full version for privacy and security
reasons.

Even with setup capability, though, you may not get updates to the
remote configuration, which brings us to...


## Configuration Drift

Fossil allows the local configuration in certain areas to differ from
that of the remote. With the exception of the prior item, you get a copy
of these configuration areas on initial clone, but after that, some
remote configuration changes don’t sync down automatically, such as the
remote’s skin. You have to ask for updates to these configuration areas
explicitly.


## Shunned Artifacts

Fossil purposefully doesn’t sync [shunned artifacts][shun]. If you want
your local clone to be a precise match to the remote, it needs to track
changes to the shun table as well.


## Universioned Artifacts

Data in Fossil’s [unversioned artifacts table][uv] doesn’t sync down by
default unless you specifically ask for it. Like local configuration
data, it doesn’t get pulled as part of a normal `fossil sync`, but
*unlike* the config data, you don’t get unversioned files as part of the
initial clone unless you ask for it by passing the `--unversioned/-u`
flag.


# Solutions

The following script solves all of the above problems for the use case
where you want a *complete* clone of the remote repository using nothing
but the normal Fossil sync protocol. It requires that you be logged into
the remote as a user with Setup capability.

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
`fossil conf pull shun`, so your repo won’t offer the shunned artifacts
to others cloning from it, but the backup can’t be said to be “complete”
if it contains information that the remote now lacks.

Alternately, if you have access to the remote server, you could get a
SQL-level backup if you’re using Fossil 2.12 or newer by using its new
[`backup` command][bu]. You could get an off-machine backup of a remote
server over SSH like so:

----

``` shell
#!/bin/bash
bf=repo-$(date +%Y-%m-%d).fossil
ssh example.com "cd museum ; fossil backup -R repo.fossil backups/$bf" &&
    scp example.com/museum/backups/$bf ~/museum/backups
```


## Encrypted Off-Site Backups

A useful refinement that you can apply to both methods above is
encrypted off-site backups. You may wish to store backups of your
repositories off-site on services such as Dropbox, Google Drive, iCloud,
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
than work around the lack of these features. If you’re on macOS, which
still ships 1.0 as of the time of this writing, [Homebrew][hb] offers
the current version of OpenSSL, but to avoid a conflict with the platform
version it’s [unliked][hbul] by default, so you have to give an explicit
path to its “cellar” directory:

       /usr/local/Cellar/openssl\@1.1/1.1.1g/bin/openssl ...

Change the `pass` value to some other long random string, and change the
`iter` value to something between 10000 and 100000. A good source for
the first is [here][grcp], and for the second, [here][rint].

Compressing the data before encrypting it removes redundancies that can
make decryption easier, and it results in a smaller backup than you get
with the previous script alone, at the expense of a lot of CPU time
during the backup.

[bu]:    /help?cmd=backup
[grcp]:  https://www.grc.com/passwords.htm
[hb]:    https://brew.sh
[hbul]:  https://docs.brew.sh/FAQ#what-does-keg-only-mean
[rint]:  https://www.random.org/integers/?num=1&min=10000&max=100000&col=5&base=10&format=html&rnd=new
[setup]: ./caps/admin-v-setup.md#apsu
[shun]:  ./shunning.wiki
[uv]:    ./unvers.wiki
