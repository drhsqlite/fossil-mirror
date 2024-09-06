# Using stunnel with Fossil on Windows

While there are many ways to configure Fossil as a server using various web
servers (Apache, IIS, nginx, etc.), this document will focus on setting up a
minimal Fossil server using only Fossil's native [server
capabilities](../any/none.md) and [stunnel](https://www.stunnel.org/)
to provide a TLS proxy.  It is recommended for public repositories to go to the
extra step of configuring stunnel to provide a proper HTTPS setup.

## Assumptions

1. You have Administrative access to a Windows 2012r2 or above server.
2. You have PowerShell 5.1 or above installed.
3. You have acquired a certificate either from a Public CA or an Internal CA.

## Configure Fossil Service for https

Due to the need for the `--https` option for successfully using Fossil with
stunnel, we will use [Advanced service installation using PowerShell](./service.md#PowerShell).
We will need to change the command to install the Fossil Service to configure
it properly for use with stunnel as an https proxy.  Run the following:

```PowerShell
New-Service -Name fossil-secure -DisplayName fossil-secure -BinaryPathName '"C:\Program Files\FossilSCM\fossil.exe" server --localhost --port 9000 --https --repolist "D:/Path/to/Repos"' -StartupType Automatic
```

The use of `--localhost` means Fossil will only listen for traffic on the local
host on the designated port - 9000 in this case - and will not respond to
network traffic.  Using `--https` will tell Fossil to generate HTTPS URLs rather
than HTTP ones.

`New-Service` does not automatically start a service on install, so you will
need to enter the following to avoid rebooting the server:

```PowerShell
Start-Service -Name fossil-secure
```

To remove the service, run the following in a Powershell or cmd console:

```
sc.exe delete fossil
```

or (in a Powershell console)

```PowerShell
Remove-Service -Name fossil
```

if your version of Powershell is 6.0 or above.

## Install stunnel 5.55

Download stunnel from the [downloads](https://www.stunnel.org/downloads.html)
page.  Select the latest stunnel windows package (at the time of writing this is
`stunnel-5.55-win64-installer.exe`).  Execute the installer and make sure you
install openSSL tools when you install stunnel.  You will need this to convert
your certificate from PFX to PEM format.

Even though the installer says it is for win64, it installs stunnel by default
to `\Program Files (x86)\stunnel`.

## Get your certificate ready for Stunnel

Whether you use a Public Certificate Authority or Internal Certificate
Authority, the next step is exporting the certificate from Windows into a format
useable by Stunnel.

### Export Certificate from Windows

If your certificate is installed via Windows Certificate Management, you will
need to export the certificate into a usable format.  You can do this either
using the Windows Certificate Management Console, or PowerShell.

#### Certificate Management Console

Start `mmc.exe` as an Administrator.  Select 'File>Add/Remove Snapin', select
'Certificates' from the list, and click 'Add'.  Select 'Computer Account',
'Next', 'Local Computer', and then 'Finish'.  In the Console Root, expand
'Certificates', then 'Personal', and select 'Certificates'.  In the middle pane
find and select your certificate.  Right click the certificate and select
'All Tasks>Export'.  You want to export as PFX the Private Key, include all
certificates in the certification path, and use a password only to secure the
file.  Enter a path and file name to a working directory and complete the
export.

Continue with [Convert Certificate from PFX to PEM](#convert).

#### PowerShell

If you know the Friendly
Name of the Certificate this is relatively easy.  Since you need to export
the private key as well, you must run the following from an Administrative
PowerShell console.

```PowerShell
$passwd = ConvertTo-SecureString -string "yourpassword" -Force -AsPlainText

Get-ChildItem Cert:\LocalMachine\My | Where{$_.FriendlyName -eq "FriendlyName"} |
Export-PfxCertificate -FilePath fossil-scm.pfx -Password $passwd
```

You will now have your certificate stored as a PFX file.

<a id="convert"></a>
### Convert Certificate from PFX to PEM

For this step you will need the openssl tools that were installed with stunnel.

```PowerShell
# Add stunnel\bin directory to path for this session.
$env:PATH += ";${env:ProgramFiles(x86)}\stunnel\bin"
# Export Private Key
openssl.exe pkcs12 -in fossil-scm.pfx -out fossil-scm.key -nocerts -nodes
# Export the Certificate
openssl.exe pkcs12 -in fossil-scm.pfx -out fossil-scm.pem -nokeys
```

Now move `fossil-scm.key` and `fossil-scm.pem` to your stunnel config directory
(by default this should be located at `\Program Files (x86)\stunne\config`).

## stunnel Configuration

Use the reverse proxy configuration given in the generic [Serving via
stunnel document](../any/stunnel.md#proxy). On Windows, the
`stunnel.conf` file is located at `\Program Files (x86)\stunnel\config`.

You will need to modify it to point at the PEM and key files generated
above.

After completing the above configuration restart the stunnel service in Windows
with the following:

```PowerShell
Restart-Service -Name stunnel
```

## Open up port 443 in the Windows Firewall

The following instructions are for the [Windows Advanced
Firewall](https://docs.microsoft.com/en-us/windows/security/threat-protection/windows-firewall/windows-firewall-with-advanced-security).
If you are using a different Firewall, please consult your Firewall
documentation for how to open port 443 for inbound traffic.

The following command should be entered all on one line.

```PowerShell
New-NetFirewallRule -DisplayName "Allow Fossil Inbound" -Description "Allow Fossil inbound on port 443 using Stunnel as TLS Proxy."
  -Direction Inbound -Protocol TCP -LocalPort 443 -Action Allow -Program "C:\Program Files (x86)\Stunnel\bin\stunnel.exe"
```

You should now be able to access your new Fossil Server via HTTPS.


*[Return to the top-level Fossil server article.](../)*
