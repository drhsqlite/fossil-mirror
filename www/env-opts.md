Environment Variables and Global Options
========================================

Fossil uses a number of environment variables and supports a number
of global options. Most of these seem to be primarily documented in
the source code, with the primary exception of `--args` which is
described in the usage text printed by running fossil with no
arguments at all.



Global Options
--------------

The following options are understood by the fossil command itself, and
are collected before any subcommand begins processing.

`--args FILENAME`: Read the file `FILENAME` and replace these two
arguments with its content. Each line of the file is assumed to be an
argument unless it starts with '-' and contains a space, in which case
it is assumed to be another flag and is treated as such. `--args
FILENAME` may be used in conjunction with any other flags.

`--case-sensitive BOOL`: Override the `case-sensitive` setting, which
can override the native preferences of the platform: insensitive on
WIndows, sensitive on Unix.

`--chdir DIRECTORY`:

`--comfmtflags NUMBER`:


`--errorlog ERRLOG`:

`--help`: If `--help` is found anywhere on the command line, translate
the command to `fossil help cmdname` where `cmdname` is the first
argument that does not begin with a `-` character.  If all arguments
start with `-`, translate to `fossil help argv[1] argv[2]...`. 

`--httptrace`:

`--localtime`:

`--nocgi`: Prevent fossil from acting as a CGI by default even if the
`GATEWAY_INTERFACE` environment variable is set.

`--no-th-hook`:

`--quiet`:

`--sqltrace`:

`--sqlstats`:

`--sshtrace`:

`--ssl-identity SSLIDENTITY`:

`--systemtrace`:

`--user LOGIN`: Also `-U LOGIN`.

`--utc`:

`--vfs VFSNAME`: Load the named VFS into SQLite.


Environment Variables
---------------------


`APPDATA`: (Windows) Location of the `~/.fossil` file. The first
environment variable found in the environment from the list
`FOSSIL_HOME`, `LOCALAPPDATA` (Windows), `APPDATA` (Windows),
`HOMEDRIVE` and `HOMEPATH` (Windows, used together), and `HOME` is
used as the location of the `~/.fossil` file. 

`EDITOR`: Name the editor to use for check-in and stash comments.
Overridden by the local or global `editor` setting or the `VISUAL`
environment variable.

`FOSSIL_HOME`: Location of the `~/.fossil` file. The first environment
variable found in the environment from the list `FOSSIL_HOME`,
`LOCALAPPDATA` (Windows), `APPDATA` (Windows), `HOMEDRIVE` and
`HOMEPATH` (Windows, used together), and `HOME` is used as the
location of the `~/.fossil` file. 

`FOSSIL_USER`: Name of the default user account if the local or global
`default-user` setting is not present. The first environment variable
found in the environment from the list `FOSSIL_USER`, `USERNAME`
(Windows), `USER`, and `LOGNAME` is the user name. If none of those
are set, then the default user name is "root".

`FOSSIL_VFS`: Name a VFS to load into SQLite. 

`GATEWAY_INTERFACE`: If present and the `--nocgi` option is not, assume
fossil is invoked from a web server as a CGI command, and act
accordingly.

`HOME`: Location of the `~/.fossil` file. The first environment
variable found in the environment from the list `FOSSIL_HOME`,
`LOCALAPPDATA` (Windows), `APPDATA` (Windows), `HOMEDRIVE` and
`HOMEPATH` (Windows, used together), and `HOME` is used as the
location of the `~/.fossil` file. 

`HOMEDRIVE`, `HOMEPATH`: (Windows) Location of the `~/.fossil` file.
The first environment variable found in the environment from the list
`FOSSIL_HOME`, `LOCALAPPDATA` (Windows), `APPDATA` (Windows),
`HOMEDRIVE` and `HOMEPATH` (Windows, used together), and `HOME` is
used as the location of the `~/.fossil` file. 

`HTTP_HOST`: If defined, included in error log messages.

`HTTP_USER_AGENT`: If defined, included in error log messages.


`LOCALAPPDATA`: (Windows) Location of the `~/.fossil` file. The first
environment variable found in the environment from the list
`FOSSIL_HOME`, `LOCALAPPDATA` (Windows), `APPDATA` (Windows),
`HOMEDRIVE` and `HOMEPATH` (Windows, used together), and `HOME` is
used as the location of the `~/.fossil` file. 

`LOGNAME`: Name of the default user account if the local or global
`default-user` setting is not present. The first environment variable
found in the environment from the list `FOSSIL_USER`, `USERNAME`
(Windows), `USER`, and `LOGNAME` is the user name. If none of those
are set, then the default user name is "root".

`PATH`: Used by most platforms to locate programs invoked without a
fully qualified name. Explicitly used by `fossil ui` on certain platforms
to choose the browser to launch.

`PATH_INFO`: If defined, included in error log messages.

`QUERY_STRING`: If defined, included in error log messages.

`REMOTE_ADDR`: If defined, included in error log messages.

`REMOTE_HOST`: Used by `fossil http` run from `stunnel` to identify
the remote host.

`REQUEST_METHOD`: If defined, included in error log messages.

`REQUEST_URI`: If defined, included in error log messages.

`SCRIPT_NAME`: If defined, included in error log messages.

`SSH_CONNECTION`:

`SYSTEMROOT`: (Windows) Used to locate `notepad.exe` as a
fall back comment editor.

`TEMP`: On Windows, the location of temporary files. The first
environment variable found in the environment that names an existing
directory from the list `TMP`, `TEMP`, `USERPROFILE`, the Windows
directory (usually `C:\WINDOWS`), `TEMP`, `TMP`, and the current
directory (aka `.`) is the temporary folder. 

`TERM`: If the linenoise library is used (almost certainly not on
Windows), it will check `TERM` to verify that the interactive terminal
is not named on a short list on terminals known to not work with
linenoise. Linenoise is a library that provides command history and
command line editing to interactive programs, and can be used in the
`fossil sqlite3` command.

`TH1_DELETE_INTERP`: Set this variable to ask fossil to explicitly
delete the TH1 interpreter, if it is loaded, then check that it
released all of its allocated memory, when exiting fossil. This is not
strictly necessary, but makes debugging memory leaks easier. See
[main.c near line 386](/artifact/e75796be5338a81c?ln=386,391) for the
code.


`TMP`: On Windows, the location of temporary files. The first
environment variable found in the environment that names an existing
directory from the list `TMP`, `TEMP`, `USERPROFILE`, the Windows
directory (usually `C:\WINDOWS`), `TEMP`, `TMP`, and the current
directory (aka `.`) is the temporary folder. 


`USER`: Name of the default user account if the local or global
`default-user` setting is not present. The first environment variable
found in the environment from the list `FOSSIL_USER`, `USERNAME`
(Windows), `USER`, and `LOGNAME` is the user name. If none of those
are set, then the default user name is "root".

`USERNAME`: Name of the default user account if the local or global
`default-user` setting is not present. The first environment variable
found in the environment from the list `FOSSIL_USER`, `USERNAME`
(Windows), `USER`, and `LOGNAME` is the user name. If none of those
are set, then the default user name is "root".

`USERPROFILE`: On Windows, the location of temporary files. The first
environment variable found in the environment that names an existing
directory from the list `TMP`, `TEMP`, `USERPROFILE`, the Windows
directory (usually `C:\WINDOWS`), `TEMP`, `TMP`, and the current
directory (aka `.`) is the temporary folder. 

`VISUAL`: Name the editor to use for check-in and stash comments.
Overrides the `EDITOR` environment variable. Overridden by the local
or global `editor` setting.







Notes on Related Values
-----------------------

### CGI and JSON Parameters


The JSON API implementation looks up many values in the first of
several places searched. This unifies the parameter handling logic,
allows the caller to choose whether to prefer URL parameters or the
POST payload, and allows the `fossil json` command to share most of
the same logic as the `/json` API path. The search order is a POST
payload, GET/COOKIE/non-JSON POST, JSON POST, the system environment.

See the comment above the implementation of [`json_getenv`][json.c]
for some further discussion.

[json.c]: /artifact/6df1d80dece8968b?ln=277,290

### Comment Editor

The editor used to edit a check-in or stash comment is named by the
local or global setting `editor`. If neither is set, then the environment
variables `VISUAL`, and `EDITOR` are checked in that order.

On Windows, if no editor is named, then Notepad is used. Note that the
operation will be aborted if `notepad.exe` is not found in the Windows
folder.

On Unix-like platforms, if no editor is named, then a message is
displayed on stdout, and stdin is read until a single line containing
only a dot is seen.


### Default Username

When creating a new repository, fossil wants to guess a sensible user
name to make the default user granted the "s" permission.

Fossil will use the setting `default-user` if set. Normally, a local
setting would override a global setting, but when creating a new
repository it is more than a little unlikely that there is an open
checkout to provide the local setting. 

**TODO:** Any interaction caused by nesting repositories is not
documented, but should be. Similarly for simply having the current
directory inside a checkout regardless of whether the created repo
will be nested.

If `default-user` is not set, then the first found environment
variable from the list `FOSSIL_USER`, `USERNAME` (Windows), `USER`,
and `LOGNAME` is the user name. If none of those are set, then the
default user name is "root".

### Error logging

If logging errors to a file, fossil will include the values of the
following environment variables in the error log entry if they are
defined: `HTTP_HOST`, `HTTP_USER_AGENT`, `PATH_INFO`, `QUERY_STRING`,
`REMOTE_ADDR`, `REQUEST_METHOD`, `REQUEST_URI`, and `SCRIPT_NAME`. 


### Home Directory

Fossil keeps some information interesting to each user in the user's
home directory. This includes the global settings and the list of
repositories and checkouts used by `fossil all`.

The user's home directory is specified by the first environment
variable found in the environment from the list `FOSSIL_HOME`,
`LOCALAPPDATA` (Windows), `APPDATA` (Windows), `HOMEDRIVE` and
`HOMEPATH` (Windows, used together), and `HOME`. 

### SQLite VFS to use

See [the SQLite documentation](http://www.sqlite.org/vfs.html) for an
explanation of what a VFS actually is and what it does.

If the default VFS underneath SQLite is not suitable, an alternative
can be selected with either the `--vfs VFSNAME` option or the
`FOSSIL_VFS` environment variable. The `--vfs` option takes
precedence.


### Temporary File Location

Fossil places some temporary files in the current directory, notably
supporting files related to merge conflicts are placed in the same
folder as the merge result.

Other temporary files need a home. On Unix-like systems, the first
folder from the hard coded list `/var/tmp`, `/usr/tmp`, `/tmp`,
`/temp`, and `.` that is found to exist in the file system is used by
fossil. 

On Windows, fossil calls [`GetTempPath`][gtp], and also queries the
environment variables `TEMP`, and `TMP`. If none of those three places
exist, then it uses `.`. Notice that `GetTempPath` itself used `TMP`,
`TEMP`, `USERPROFILE`, and the Windows folder (named in the variable
`SystemRoot`). Since the Windows folder always exists, but in modern
versions of Windows is generally *not* writable by the logged in user,
not having `TEMP`, `TMP`, or `USERPROFILE` set is almost guaranteed to
cause trouble.

[gtp]: https://msdn.microsoft.com/en-us/library/windows/desktop/aa364992%28v=vs.85%29.aspx 

That said, it is not unusual for utilities on all platforms to assume
that `TEMP` or `TMP` point somewhere safe for temporary files.

If the identified temporary folder is not writable, then weird things
will happen on all platforms.


### Web browser

Occasionally, fossil wants to launch a web browser for the user, most
obviously as part of the `fossil ui` command. In that specific case,
the browser is launched pointing at the web server started by `fossil
ui` listening on a private TCP port.

On all platforms, if the local or global settings `web-browser` is
set, that is the command used to open an URL.

Otherwise, the specific actions vary by platform.

On Unix-like platforms other than Apple's, it looks for the first
program from the list `xdg-open`, `gnome-open`, `firefox`, and
`google-chrome` that it can find on the `PATH`.

On Apple platforms, it assumes that `open` is the command to open an
URL in the user's configured default browser.

On Windows platforms, it assumes that `start` is the command to open
an URL in the user's configured default browser.
