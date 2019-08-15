# Fossil as a Windows Service

If you need Fossil to start automatically on Windows, it is suggested to install
Fossil as a Windows Service.

## Assumptions

1. You have Administrative access to a Windows 2012r2 or above server.
2. You have PowerShell 5.1 or above installed.

## Place Fossil on Server

However you obtained your copy of Fossil, it is recommended that you follow
Windows conventions and place it within `\Program Files (x86)\FossilSCM`.  Since
Fossil is a 32bit binary, this is the proper location for the executable.  This
way Fossil is in an expected location and you will have minimal issues with
Windows interfering in your ability to run Fossil as a service.  You will need
Administrative rights to place fossil at the recommended location.  You do NOT
need to add this location to the path, though you may do so if you wish.

## Make Fossil a Windows Service

Luckily the hard work to use Fossil as a Windows Service has been done by the
Fossil team.  We simply have to install it with the proper command line options.
As of Fossil 2.9 the built in `fossil winsrv` command is failing, so an
alternative service install using PowerShell is documented here.  The below
should all be entered as a single line in an Administrative PowerShell console.

```PowerShell
New-Service -Name fossil -DisplayName fossil -BinaryPathName '"C:\Program Files (x86)\FossilSCM\fossil.exe"
server --port 8080 --repolist "D:/Path/to/Repos"' -StartupType Automatic
```

Please note the use of forward slashes in the paths passed to Fossil.  Windows
will accept either back slashes or forward slashes in path names, but Fossil has
a preference for forward slashes.  The use of `--repolist` will make this a
multiple repository server.  If you want to serve only a single repository,
then leave off the `--repolist` parameter and provide the full path to the
proper repository file. Other options are listed in the
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
