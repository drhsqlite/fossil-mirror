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
this has been fixed.  Commands like

  * "[fossil server](/help?cmd=server)"
  * "[fossil ui](/help?cmd=ui)", and
  * "[fossil http](/help?cmd=http)"

now all handle server-mode SSL/TLS encryption natively.  It is now possible
to run a secure Fossil server without having to put Fossil behind an encrypting
web server or reverse proxy.  Hence, it is now possible to stand up a complete
Fossil project website on an inexpensive VPS with no added software other than
Fossil itself and something like [certbot](https://certbot.eff.org) for
obtaining a CA-signed certificate.

## Usage

To put any of the Fossil server commands into SSL/TLS mode, simply
add the "--cert" command-line option.

    fossil ui --cert unsafe-builtin

The --cert option is what tells Fossil to use TLS encryption.
Normally, the argument to --cert is the name of a file containing
the certificate (the "fullchain.pem" file) for the website.  In this
example, the magic name "unsafe-builtin" is used, which causes Fossil
to use a self-signed cert rather than a real cert obtained from a
[Certificate Authority](https://en.wikipedia.org/wiki/Certificate_authority)
or "CA".  As the name implies, this self-signed cert is not secure and
should only be used for testing.  Your web-browser will complain bitterly 
and will refuse to display the pages using the "unsafe-builtin" cert.
Firefox will allow you to click an "I know the risks" button and continue.
Other web browsers will stubornly refuse to display the page, under the theory
that weak encryption is worse than no encryption at all.  Continue reading
to see how to solve this.

## About Certs

Certs are based on public-key or asymmetric cryptography.  To create a cert,
you first create a new "key pair" consisting of a public key and a private key.
The public key can be freely shared with the world, but you must keep the
private key secret.  If anyone gains access to your private key then he will be
able to impersonate you and break into your system.

To obtain a cert, you send your public key and the name of the domain you
want to protect to a certificate authority.  The CA then digitally signs
the combination of those two things using their own private key and sends
the signed combination back to you.  The CA's digital signature of your
public key and domain name is the cert.

SSL/TLS servers need two things in order to prove their identity to clients:

  1.  The cert that was signed by a CA
  2.  The private key

The SSL/TLS servers send the cert to each client, so that the client
can verify it.  But the private key is kept strictly private and is never
shared with anyone.

## How To Tell Fossil About Your Cert And Private Key

If you do not have your own cert and private key, you can ask Fossil
to use "unsafe-builtin", which is a self-signed cert that is built into
Fossil.  This is wildly insecure, since the private key is not really private - 
it is [in plain sight](/info/c2a7b14c3f541edb96?ln=89-116) in the Fossil
source tree for anybody to read.  <b>Never add the private key that is
built into Fossil to your OS's trust store</b> as doing so will severely
compromise your computer.  The built-in cert is only useful for testing.
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
certificate, you can concatenate the two into a single file and the
individual components will still be easily accessible.

If you have a single file that holds both your private key and your
cert, you can hand it off to the "[fossil server](/help?cmd=server)"
command using the --cert option.  Like this:

    fossil server --port 443 --cert mycert.pem /home/www/myproject.fossil

The command above is sufficient to run a fully-encrypted web site for
the "myproject.fossil" Fossil repository.  This command must be run as
root, since it wants to listen on TCP port 443, and only root processes are
allowed to do that.  This is safe, however, since before reading any
information off of the wire, Fossil will put itself inside a chroot jail
at /home/www and drop all root privileges.

### Keeping The Cert And Private Key In Separate Files

If you do not want to combine your cert and private key into a single
big PEM file, you can keep them separate using the --pkey option to
Fossil.

    fossil server --port 443 --cert fullchain.pem --pkey privkey.pem /home/www/myproject.fossil

## The ACME Protocol

The [ACME Protocol][2] is used to prove to a CA that you control a
website.  CAs require proof that you control a domain before they
will issue a cert for that domain.  The usual means of dealing
with ACME is to run the separate [certbot](https://certbot.eff.org) tool.
Here is, in a nutshell, what certbot will do to obtain your cert:

  1.  Certbot sends your "signing request" (the document that contains
      your public key and your domain name) to the CA.

  2.  After receiving the signing request, the CA needs to verify that you
      control the domain of the cert.  To do this (or, one common
      way of doing this, at least) the CA sends a secret token back to
      certbot through a secure backchannel, and instructs certbot to make
      that token accessible on the (unencrypted, ordinary "http:") web site
      for the domain in a particular file under the ".well-known" subdirectory.

  3.  Certbot puts the token where the CA requested it, then notifies the
      CA that it is there.

  4.  The CA accesses the token to confirm that you do indeed control the
      website.  It then creates the cert and sends it back to certbot.

  5.  Certbot stores your cert and deletes the ".well-known" token.

In order for all of this to happen, certbot needs to be able to create
a subdirectory named ".well-known", within a directory you specify, and
then populate that subdirectory with a token file of some kind.  To support
this, the "[fossil server](/help?cmd=server)" and
"[fossil http](/help?cmd=http)" commands have the --acme option.
When the --acme option is specified and Fossil sees a URL where the path
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

Once certbot has obtained your cert, then you can concatenate that
cert with your private key and run Fossil in SSL/TLS mode as shown above.

[2]: https://en.wikipedia.org/wiki/Automated_Certificate_Management_Environment
