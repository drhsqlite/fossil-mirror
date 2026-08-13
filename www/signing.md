# Signing Check-ins

Fossil can sign check-in manifests. A basic concept in public-key
cryptography, signing can bring some advantages such as authentication and
non-repudiation. In practice, a serious obstacle is the public key
infrastructure – that is, the problem of reliably verifying that a given
public key belongs to its supposed owner (also known as _"signing is easy,
verifying is hard"_).

Fossil neither creates nor verifies signatures by itself, instead relying on
external tools that have to be installed side-by-side. Historically, the tool
most employed for this task was [GnuPG](https://gnupg.org); recently, there has
been an increase in the usage of [OpenSSH](https://openssh.com) (the minimum
required version is 8.1, released on 2019-10-09).

## Signing a check-in

The `clearsign` setting must be on; this will cause every check-in to be signed
(unless you provide the `--nosign` flag to `fossil commit`). To this end,
Fossil calls the command given by the `pgp-command` setting.

Fossil needs a non-detached signature that includes the rest of the usual
manifest. For GnuPG, this is no problem, but as of 2025 (version 9.9p1) OpenSSH
can create **and verify** only detached signatures; Fossil itself must
attach this signature to the manifest prior to committing. This makes the 
verification more complex, as additional steps are needed to extract the
signature and feed it into OpenSSH.

### GnuPG

The `pgp-command` setting defaults to
`gpg --clearsign -o`.
(A possible interesting option to `gpg --clearsign` is `-u`, to specify the
user to be used for signing.)

### OpenSSH

A reasonable value for `pgp-command` is

```
ssh-keygen -q -Y sign -n fossilscm -f ~/.ssh/id_ed25519
```

for Linux, and

```
ssh-keygen -q -Y sign -n fossilscm -f %USERPROFILE%/.ssh/id_ed25519
```

for Windows, changing as appropriate `-f` to the path of the private key to be
used.

The value for `-n` (the _namespace_) can be changed at will, but care has to be
taken to use the same value when verifying the signature.

Fossil versions prior to 2.26 do not understand SSH signatures and
will treat artifacts signed this way as opaque blobs, not Fossil
artifacts.


## Verifying a signature

Fossil does not provide an internal method for verifying signatures and
relies – like it does for signing – on external tools.

### GnuPG

Assuming you used the
default GPG command for signing, one can verify the signature using

```
fossil artifact <CHECK-IN> | gpg --verify
```

### OpenSSH

The user and the key that was used to create the signature must be listed
together in the `ALLOWED_SIGNERS` file (see
[documentation](https://man.openbsd.org/ssh-keygen#ALLOWED_SIGNERS)).
Note that in that file, the "@DOMAIN" bit for the principal is only a
recommendation; you can (or even _should_) simply use your Fossil user name.

As mentioned, for lack of an OpenSSH built-in non-detached signature mechanism,
the burden of extracting the relevant part of the signed check-in is on the
user.

The following recipes are provided only as examples and can be easily extended 
to fully-fledged scripts.

#### For Linux:

```bash
fsig=$(mktemp /tmp/__fsig.XXXXXX) && \
fusr=$(fossil artifact tip \
  | awk -v m="${fsig}" -v s="${fsig}.sig" \
      '/^-----BEGIN SSH SIGNED/{of=m;next} \
       /^-----BEGIN SSH SIGNATURE/{of=s} \
       /^U /{usr=$2} \
       /./{if(!of){exit 42};print >> of} END{print usr}') && \
ssh-keygen -Y verify -f ~/.ssh/allowed_signers -I ${fusr} -n fossilscm \
  -s "${fsig}.sig" < "${fsig}" || echo "No SSH signed check-in" && \
rm -f "${fsig}.sig" "${fsig}" && \
unset -v fsig fusr
```

#### For Windows (cmd):

The following incantation makes use of `awk` and `dos2unix`, standard Unix
tools but requiring separate installation on Windows (for example,using
[BusyBox](https://frippery.org/busybox/#downloads)). The usage of `awk` can be
replaced with the Windows basic tool `findstr`, leading to a longer recipe.

```bat
fossil artifact <CHECK-IN> | awk -v m="__fsig" -v s="__fsig.sig" ^
  "/^-----BEGIN SSH SIGNED/{of=m;next} /^-----BEGIN SSH SIGNATURE/{of=s} /./{if(!of){exit 42};print >> of}"
if %errorlevel% equ 42 (echo No SSH signed check-in)
REM ---Skip remaining lines if no SSH signed message---
for /f "tokens=2" %i in ('findstr /b "U " __fsig') do set fusr=%i
dos2unix __fsig __fsig.sig
ssh-keygen -Y verify -f %USERPROFILE%\.ssh\allowed_signers -I "%fusr%" ^
  -n fossilscm -s __fsig.sig < __fsig
del __fsig __fsig.sig 2>nul & set "fusr="
```

