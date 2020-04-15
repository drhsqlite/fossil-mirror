# Fossil as a Windows Service

If you need Fossil to start automatically on Windows, it is suggested to install
Fossil as a Windows Service.

## Assumptions

1. You have Administrative access to a Windows 2012r2 or above server.
2. You have PowerShell 5.1 or above installed.

## Place Fossil on Server

However you obtained your copy of Fossil, it is recommended that you follow
Windows conventions and place it within `\Program Files\FossilSCM`.  Since
Fossil 2.10 is a 64bit binary, this is the proper location for the executable.
This way Fossil is at an expected location and you will have minimal issues with
Windows interfering in your ability to run Fossil as a service.  You will need
Administrative rights to place fossil at the recommended location.  If you will
only be running Fossil as a service, you do not need to add this location to the
path, though you may do so if you wish.

## Installing Fossil as a Service

Luckily the hard work to use Fossil as a Windows Service has been done by the
Fossil team.  We simply have to install it with the proper command line options.
Fossil on Windows has a command `fossil winsrv` to allow installing Fossil as a
service on Windows.  This command is only documented on the windows executable
of Fossil.  You must also run the command as administrator for it to be
successful.

### Fossil winsrv Example

The simplest form of the command is:

```
fossil winsrv create --repository D:/Path/to/Repo.fossil
```

This will create a windows service named 'Fossil-DSCM' running under the local
system account and accessible on port 8080 by default.  `fossil winsrv` can also
start, stop, and delete the service.  For all available options, please execute
`fossil help winsrv` on a windows install of Fossil.

If you wish to server a directory of repositories, the `fossil winsrv` command
requires a slightly different set of options vs. `fossil server`:

```
fossil winsrv create --repository D:/Path/to/Repos --repolist
```

<a name='PowerShell'></a>
### Advanced service installation using PowerShell

As great as `fossil winsrv` is, it does not have one to one reflection of all of
the `fossil server` [options](/help?cmd=server).  When you need to use some of
the more advanced options, such as `--https`, `--skin`, or `--extroot`, you will
need to use PowerShell to configure and install the Windows service.

PowerShell provides the [New-Service](https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.management/new-service?view=powershell-5.1)
command, which we can use to install and configure Fossil as a service.  The
below should all be entered as a single line in an Administrative PowerShell
console.

```PowerShell
New-Service -Name fossil -DisplayName fossil -BinaryPathName '"C:\Program Files\FossilSCM\fossil.exe" server --port 8080 --repolist "D:/Path/to/Repos"' -StartupType Automatic
```

Please note the use of forward slashes in the repolist path passed to Fossil.
Windows will accept either back slashes or forward slashes in path names, but
Fossil has a preference for forward slashes.  The use of `--repolist` will make
this a multiple repository server.  If you want to serve only a single
repository, then leave off the `--repolist` parameter and provide the full path
to the proper repository file. Other options are listed in the
[fossil server](/help?cmd=server) documentation.

The service will be installed by default to use the Local Service account.
Since Fossil only needs access to local files, this is fine and causes no
issues.  The service will not be running once installed.  You will need to start
it to proceed (the `-StartupType Automatic` parameter to `New-Service` will
result in the service auto-starting on boot).  This can be done by entering

```PowerShell
Start-Service -Name fossil
```

in the PowerShell console.

Congratulations, you now have a base http accessible Fossil server running on
Windows.

*[Return to the top-level Fossil server article.](../)*
