# SSL/TLS Server Mode

## History

Fossil has supported [client-side SSL/TLS][0] since [2010][1].  This means
that commands like "[fossil sync](/help?cmd=sync)" could use SSL/TLS when
contacting a server.  But on the server side, commands like
"[fossil server](/help?cmd=server)" operated in clear-text only.  To implement
an encrypted server, you had to put Fossil behind a web server or reverse
proxy that handled the SSL/TLS decryption/encryption and passed cleartext
down to Fossil.

[0]: ./ssl.wiki
[1]: /timeline?c=b05cb4a0e15d0712&y=ci&n=13

Beginning in [late December 2021](/timeline?c=f6263bb64195b07f&y=a&n=13),
Fossil servers are now able to converse directly over TLS.  Commands like

  * "[fossil server](/help?cmd=server)"
  * "[fossil ui](/help?cmd=ui)", and
  * "[fossil http](/help?cmd=http)"

may now handle the encryption natively when suitably configured, without
requiring a third-party proxy layer.

## <a id="usage"></a>Usage

To put any of the Fossil server commands into SSL/TLS mode, simply
add the "`--cert`" command-line option:

    fossil ui --cert unsafe-builtin

Here, we are passing the magic name "unsafe-builtin" to cause Fossil to
use a [hard-coded self-signed cert][hcssc] rather than one obtained from
a recognized [Certificate Authority][CA], or "CA".

As the name implies, this self-signed cert is _not secure_ and should
only be used for testing. Your web browser is likely to complain
bitterly about it and will refuse to display the pages using the
"unsafe-builtin" cert until you placate it. The complexity of the
ceremony demanded depends on how paranoid your browser’s creators have
decided to be. It may require as little as clicking a single big "I know
the risks" type of button, or it may require a sequence be several
clicks designed to discourage the “yes, yes, just let me do the thing”
crowd lest they run themselves into trouble by disregarding well-meant
warnings.

Our purpose here is to show you an alternate path that will avoid the
issue entirely, not weigh in on which browser handles self-signed
certificates best.

[CA]: https://en.wikipedia.org/wiki/Certificate_authority
[hcssc]: /info/c2a7b14c3f541edb96?ln=89-116

## <a id="about"></a>About Certs

The X.509 certificate system used by browsers to secure TLS connections
is based on asymmetric public-key cryptography. The methods for
obtaining one vary widely, with a resulting tradeoff we may summarize as
trustworthiness versus convenience, the latter characteristic falling as
the former rises.(^No strict correlation exists. CAs have invented
highly inconvenient certification schemes that offer little additional
real-world trustworthiness. Extreme cases along this axis may be fairly
characterized as [security theater][st]. We focus in this document on
well-balanced trade-offs between decreasing convenience and useful
levels of trustworthiness gained thereby.)

The self-signed method demonstrated above offers approximately zero
trustworthiness, though not zero _value_ since it does still provide
connection encryption.

More trustworthy methods are necessarily less convenient. One such is to
send your public key and the name of the domain you want to protect to a
recognized CA, which then performs one or more tests to convince itself
that the requester is in control of that domain. If the CA’s tests all
pass, it produces an X.509 certificate bound to that domain, which
includes assorted other information under the CA’s digital signature
attesting to the validity of the document’s contents. The result is sent
back to the requester, which may then use it to transitively attest to
these tests’ success: presuming one cannot fake the type of signature
used, the document must have been signed by the trusted, recognized CA.

There is one element of the assorted information included with a
certificate that is neither supplied by the requester nor rubber-stamped
on it in passing by the CA. It also generates a one-time key pair and
stores the public half in the certificate. The cryptosystem this keypair
is intended to work with varies both by the CA and by time, as older
systems become obsolete. Details aside, the CA then puts this matching
private half of the key in a separate file, often encrypted under a
separate cryptosystem for security.

SSL/TLS servers need both resulting halves to make these attestations,
but they send only the public half to the client when establishing the
connection. The client then makes its own checks to determine whether it
trusts the attestations being made.

A properly written and administered server never releases the private
key to anyone. Ideally, it goes directly from the CA to the requesting
server and never moves from there; then when it expires, the server
deletes it permanently.

[st]: https://en.wikipedia.org/wiki/Security_theater

## <a id="startup"></a>How To Tell Fossil About Your Cert And Private Key

As we saw [above](#usage),
if you do not have your own cert and private key, you can ask Fossil
to use "unsafe-builtin", which is a self-signed cert that is built into
Fossil.  This is wildly insecure, since the private key is not really private;
it is [in plain sight][hcssc] in the Fossil
source tree for anybody to read.  <b>Never add the private key that is
built into Fossil to your OS's trust store</b> as doing so will severely
compromise your computer.[^ssattack] This built-in cert is only useful for testing.
If you want actual security, you will need to come up with your own private
key and cert.

Fossil wants to read certs and public keys in the 
[PEM format](https://en.wikipedia.org/wiki/Privacy-Enhanced_Mail).
PEM is a pure ASCII text format.  The private key consists of text
like this:

    -----BEGIN PRIVATE KEY-----
    *base-64 encoding of the private key*  
    -----END PRIVATE KEY-----

Similarly, a PEM-encoded cert will look like this:

    -----BEGIN CERTIFICATE-----
    *base-64 encoding of the certificate*  
    -----END CERTIFICATE-----

In both formats, text outside of the delimiters is ignored.  That means
that if you have a PEM-formatted private key and a separate PEM-formatted
certificate, you can concatenate the two into a single file, and the
individual components will still be easily accessible.

### <a id="cat"></a>Separate or Concatenated?

Given a single concatenated file that holds both your private key and your
cert, you can hand it off to the "[fossil server](/help?cmd=server)"
command using the `--cert` option, like this:

    fossil server --port 443 --cert mycert.pem /home/www/myproject.fossil

The command above is sufficient to run a fully-encrypted web site for
the "myproject.fossil" Fossil repository.  This command must be run as
root, since it wants to listen on TCP port 443, and only root processes are
allowed to do that.  This is safe, however, since before reading any
information off of the wire, Fossil will [put itself inside a chroot
jail](./chroot.md) at `/home/www` and drop all root privileges.

This method of combining your cert and private key into a single big PEM
file carries risks, one of which is that the system administrator must
make both halves readable by the user running the Fossil server. Given
the chroot jail feature, a more secure scheme separates the halves so
that only root can read the private half, which then means that when
Fossil drops its root privileges, it becomes unable to access the
private key on disk. Fossil’s `server` feature includes the `--pkey`
option to allow for that use case:

    fossil server --port 443 --cert fullchain.pem --pkey privkey.pem /home/www/myproject.fossil

[^ssattack]: ^How, you ask? Because the keys are known, they can be used
    to provide signed certificates for **any** other domain. One foolish
    enough to tell their OS’s TLS mechanisms to trust the signing
    certificate is implicitly handing over all TLS encryption controls
    to any attacker that knows they did this. Don’t do it.

### <a id="chain"></a>Chains and Links

The file name “`fullchain.pem`” used above is a reference to a term of
art within this world of TLS protocols and their associated X.509
certificates. Within the simplistic scheme originally envisioned by the
creators of SSL — the predecessor to TLS — we were all expected to agree
on a single set of CA root authorities, and we would all agree to get
our certificates from one of them. The real world is more complicated:

*   The closest we have to universal acceptance of CAs is via the
    [CA/Browser Forum][CAB], and even within its select membership there
    is continual argument over which roots are trustworthy. (Hashing
    that out is arguably this group’s key purpose.)

*   CAB’s decision regarding trustworthiness may not match that of any
    given system’s administrator. There are solid, defensible reasons to
    prune back the stock CA root set included with your browser, then to
    augment it with ones CAB _doesn’t_ trust.

*   TLS isn’t limited to use between web browsers and public Internet
    sites. Several common use cases preclude use of the process CAB
    envisions, with servers able to contact Internet-based CA roots as
    part of proving their identity. Different use cases demand different
    CA root authority stores.

    The most common of these divergent cases are servers behind strict
    firewalls and edge devices that never interact with the public
    Internet. This class ranges from cheap home IoT devices to the
    internal equipment managed by IT for a massive global corporation.

Your private Fossil server is liable to fall into that last category.
This may then require that you generate a more complicated “chain” of
certificates for Fossil to use here, without which the client may not be
able to get back to a CA root it trusts. This is true regardless of
whether that client is another copy of Fossil or a web browser
traversing Fossil’s web UI, though that fact complicates matters by
allowing for multiple classes of client, each of which may have their
own rules for modifying the stock certificate scheme.

This is distressingly common, in fact: Fossil links to OpenSSL to
provide its TLS support, but there is a good chance that your browser
uses another TLS implementation entirely. They may or may not agree on a
single CA root store.

How you accommodate all this complexity varies by the CA and other
details. As but one example, Firefox’s “View Certificate” feature offers
_two_ ways to download a given web site’s certificate: the cert alone or
the “chain” leading back to the root. Depending on the use case, the
standalone certificate might suffice, or you might need some type of
cert chain.  Complicating this is that the last link in the chain may be
left off when it is for a mutually trusted CA root, implicitly
completing the chain.

[CAB]: https://en.wikipedia.org/wiki/CA/Browser_Forum

## <a id="acme"></a>The ACME Protocol

The [ACME Protocol][2] simplifies all this by automating the process of
proving to a recognized public CA that you are in control of a given
website. Without this proof, no valid CA will issue a cert for that
domain, as that allows fraudulent impersonation.

The primary implementation of ACME is [certbot], a product of the Let’s
Encrypt organization.
Here is, in a nutshell, what certbot will do to obtain your cert:

  1.  It sends your "signing request" (the document that contains
      your public key and your domain name) to the CA.

  2.  After receiving the signing request, the CA needs to verify that
      you control the domain of the cert. One of several methods certbot has
      for accomplishing this is to create a secret token and place it at
      a well-known location, then tell the CA about it over ACME.

  3.  The CA then tries pulling that token, which if successful proves
      that the requester is able to create arbitrary data on the server,
      implicitly proving control over that server. This must be done
      over the unencrypted HTTP protocol since TLS isn’t working yet.

  4.  If satisfied by this proof of control, the CA then creates the
      keypair described above and bakes the public half into the
      certificate it signs. It then sends this and the private half of
      the key back to certbot.

  5.  Certbot stores these halves separately for the reasons sketched
      out above.

  6.  It then deletes the secret one-time-use token it used to prove
      domain control. ACME’s design precludes replay attacks.

In order for all of this to happen, certbot needs to be able to create
a subdirectory named ".well-known", within a directory you specify,
then populate that subdirectory with a token file of some kind.  To support
this, the "[fossil server](/help?cmd=server)" and
"[fossil http](/help?cmd=http)" commands have the --acme option.

When specified, Fossil sees a URL where the path
begins with ".well-known", then instead of doing its normal processing, it
looks for a file with that pathname and returns it to the client.  If
the "server" or "http" command is referencing a single Fossil repository,
then the ".well-known" sub-directory should be in the same directory as
the repository file.  If the "server" or "http" command are run against
a directory full of Fossil repositories, then the ".well-known" sub-directory
should be in that top-level directory.

Thus, to set up a project website, you should first run Fossil in ordinary
unencrypted HTTP mode like this:

    fossil server --port 80 --acme /home/www/myproject.fossil

Then you create your public/private key pair and run certbot, giving it
a --webroot of /home/www.  Certbot will create the sub-directory
named "/home/www/.well-known" and put token files there, which the CA
will verify.  Then certbot will store your new cert in a particular file.

Once certbot has obtained your cert,  you may either pass the two halves
to Fossil separately using the `--pkey` and `--cert` options described
above, or you may concatenate them and pass that via `--cert` alone.

[2]: https://en.wikipedia.org/wiki/Automated_Certificate_Management_Environment
[certbot]: https://certbot.eff.org
