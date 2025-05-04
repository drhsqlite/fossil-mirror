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
can override the native preferences of the platform for case sensitive
file names: insensitive on Windows, sensitive on Unix. There are
probably odd interactions possible if you mix case sensitive and case
insensitive file systems on any single platform. This option or the
global setting should be used to force the case sensitivity to the
most sensible condition.

`--cgitrace`: Active CGI tracing.

`--chdir DIRECTORY`: Change to the named directory before processing
any commands.


`--comfmtflags NUMBER`: Specify flags that control how check-in comments
and certain other text outputs are formatted for display. The flags are
individual bits in `NUMBER`, which must be specified in base 10:

  * _0_ &mdash; Uses the revised algorithm with no special handling.

  * _1_ &mdash; Uses the canonical algorithm, other flags are ignored.

  * _2_ &mdash; Trims leading and trailing carriage-returns and line-feeds
        where they do not materially impact pre-existing formatting
        (i.e. at the start of the comment string _and_ right before
        line indentation).

  * _4_ &mdash; Trims leading and trailing spaces where they do not materially
        impact the pre-existing formatting (i.e. at the start of the
        comment string _and_ right before line indentation).

  * _8_ &mdash; Attempts to break lines on word boundaries while honoring the
        logical line length.

  * _16_ &mdash; Looks for the original comment text within the text being
         printed.  Upon matching, a new line will be emitted, thus
         preserving more of the pre-existing formatting.


`--comment-format NUMBER`: Alias for `--comfmtflags NUMBER`.


> NOTE: As of Fossil version 2.26, use of the `--comfmtflags` and
> `--comment-format` options is no longer recommended and they are
> no longer documented, but retained for backwards compatibility.


`--errorlog ERRLOG`: Name a file to which fossil will log panics,
errors, and warnings.


`--help`: If `--help` is found anywhere on the command line, translate
the command to `fossil help cmdname` where `cmdname` is the first
argument that does not begin with a `-` character.  If all arguments
start with `-`, translate to `fossil help argv[1] argv[2]...`.

`--httptrace`: (Sets `g.fHttpTrace`.) Trace outbound HTTP requests.

`--localtime`: Override the `timeline-utc` option to explicitly use
local time.

`--nocgi`: Prevent fossil from acting as a CGI by default even if the
`GATEWAY_INTERFACE` environment variable is set.

`--no-th-hook`: (Sets `g.fNoThHook`.) Override the `th1-hooks` setting
and prevent any TH1 hooks from being executed.

`--quiet`: (Sets `g.fQuiet`.) Cause fossil to suppress various messages and progress
indicators that would otherwise be printed.

`--sqltrace`: (Sets `g.SqlTrace`.) Implies `--sqlstats`. Trace certain
SQLite database activity, especially showing every SQL query
processed.

`--sqlstats`: (Sets `g.fSqlStats`.) Print a number of performance
statistics about each SQLite database used when it is closed.

`--sshtrace`: (Sets `g.fSshTrace`.)

`--ssl-identity`: The fully qualified name of the file containing the client
certificate and private key to use, in PEM format.  It can be created by
concatenating the client certificate and private key files.  This identity will
be presented to SSL servers to authenticate the client, in addition to the
normal password authentication.

`--systemtrace`: (Sets `g.fSystemTrace`.) Trace all commands launched
as sub processes.

`--user LOGIN`: (Sets `g.zLogin`) Also `-U LOGIN`. Set the user name
used with the repository.

`--utc`: Override the `timeline-utc` option to explicitly use
UTC time.

`--vfs VFSNAME`: Load the named VFS into SQLite.


Environment Variables
---------------------

The location of the user's account-wide [configuration database][configdb]
depends on the operating system and on the existence of various 
environment variables and/or files.  See the discussion of the
[configuration database location algorithm][configloc] for details.

`EDITOR`: Name the editor to use for check-in and stash comments.
Overridden by the local or global `editor` setting or the `VISUAL`
environment variable.

`FOSSIL_BREAK`: If set, an opportunity will be created to attach a
debugger to the Fossil process prior to any significant work being
performed.

`FOSSIL_FORCE_TICKET_MODERATION`: If set, *ALL* changes for tickets
will be required to go through moderation (even those performed by the
local interactive user via the command line).  This can be useful for
local (or remote) testing of the moderation subsystem and its impact
on the contents and status of tickets.

`FOSSIL_FORCE_WIKI_MODERATION`: If set, *ALL* changes for wiki pages
will be required to go through moderation (even those performed by the
local interactive user via the command line).  This can be useful for
local (or remote) testing of the moderation subsystem and its impact
on the contents and status of wiki pages.


`FOSSIL_HOME`: Location of [configuration database][configdb].
See the [configuration database location][configloc] description
for additional information.

`FOSSIL_REPOLIST_TITLE`: The page title of the "Repository List" page
loaded by the `fossil all ui` or `fossil ui /` commands. Only used if
none of the listed repositories has the `repolist_skin` property set.
Can be set from the [CGI control file][cgictlfile].

`FOSSIL_REPOLIST_SHOW`: If this variable exists and has a text value
that contains the substring "description", then the "Project Description"
column appears on the repolist page.  If it contains the substring
"login-group", then the Login-Group column appears on the repolist page.
Can be set from the [CGI control file][cgictlfile].

`FOSSIL_USE_SEE_TEXTKEY`: If set, treat the encryption key string for
SEE as text to be hashed into the actual encryption key.  This has no
effect if Fossil was not compiled with SEE support enabled.


`FOSSIL_USER`: Name of the default user account if the checkout, local
or global `default-user` setting is not present. The first environment
variable found in the environment from the list `FOSSIL_USER`, `USER`,
`LOGNAME`, and `USERNAME` is the user name. If none of those are set,
then the default user name is "root". See the discussion of Fossil
Username below for a lot more detail.


`FOSSIL_SECURITY_LEVEL`: If set to any of the values listed below,
additional measures for password security will be enabled (also see
[How To Use Encrypted Repositories][encryptedrepos.wiki]):

[encryptedrepos.wiki]: /doc/trunk/www/encryptedrepos.wiki

  * _≥1_ &mdash; Do not remember passwords.

  * _≥2_ &mdash; Use a scrambled matrix for password input.


`FOSSIL_TCL_PATH`: When Tcl stubs support is configured, point to a
specific file or folder containing the version of Tcl to load at run
time.

`FOSSIL_TEST_DANGEROUS_IGNORE_OPEN_CHECKOUT`: When set to the literal
value `YES_DO_IT`, the test suite will relax the constraint that some
tests may not run within an open checkout.  This is subject to removal
in the future.

`FOSSIL_VFS`: Name a VFS to load into SQLite.

`GATEWAY_INTERFACE`: If present and the `--nocgi` option is not, assume
fossil is invoked from a web server as a CGI command, and act
accordingly.

`HOME`: Potential location of the [configuration database][configdb].
See the [configuration database location][configloc] description for details.

`HOMEDRIVE`, `HOMEPATH`: (Windows) Location of the `~/.fossil` file.
The first environment variable found in the environment from the list
`FOSSIL_HOME`, `LOCALAPPDATA` (Windows), `APPDATA` (Windows),
`HOMEDRIVE` and `HOMEPATH` (Windows, used together), and `HOME` is
used as the location of the `~/.fossil` file.

`HTTP_HOST`: If defined, included in error log messages.

`http_proxy`: If the global or local settings `proxy` is not set, this
is used as the default value for the `proxy` setting.


`HTTP_USER_AGENT`: If defined, included in error log messages.


`LOCALAPPDATA`: (Windows) Location of the `~/.fossil` file. The first
environment variable found in the environment from the list
`FOSSIL_HOME`, `LOCALAPPDATA` (Windows), `APPDATA` (Windows),
`HOMEDRIVE` and `HOMEPATH` (Windows, used together), and `HOME` is
used as the location of the `~/.fossil` file.

`LOGNAME`: Name of the logged in user on many Unix-like platforms.
Used as the fossil user name if `FOSSIL_USER` is not specified. See
the discussion of Fossil Username below for a lot more detail.

`NO_COLOR`: If defined and not set to a `false` value (i.e. "off", "no",
"false", "0"), the `fossil search` command skips colorization of console
output using ANSI escape codes (VT100).

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

`SSH_CONNECTION`: Informs CGI processing if the remote client is SSH.

`SSL_CERT_FILE`, `SSL_CERT_DIR`: Override the [`ssl-ca-location`]
(/help?cmd=ssl-ca-location) setting.

`SQLITE_FORCE_PROXY_LOCKING`: From `sqlite3.c`, 1 means force always
use proxy, 0 means never use proxy, and undefined means use proxy for
non-local files only.

`SQLITE_TMPDIR`: Names the temporary file location for SQLite.  When
set, this will be used instead of `TMPDIR`.


`SYSTEMROOT`: (Windows) Used to locate `notepad.exe` as a
fall back comment editor.

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

`TH1_ENABLE_DOCS`: Override the local or global setting `tcl-docs`
to enable TH1 documents in fossil.

`TH1_ENABLE_HOOKS`: Override the local or global setting `tcl-hooks`
to enable TH1 hooks in fossil.

`TH1_ENABLE_TCL`: Override the local or global setting `tcl` to enable
Tcl in fossil.

`TH1_TEST_ANON_CAPS`: Override the default anonymous permissions used
when processing the `--set-anon-caps` option for the `test-th-eval`,
`test-th-render`, and `test-th-source` test commands.

`TH1_TEST_USER_CAPS`: Override the default user permissions used when
processing the `--set-user-caps` option for the `test-th-eval`,
`test-th-render`, and `test-th-source` test commands.

`TMPDIR`: Names the temporary file location for SQLite.


`USER`: Name of the logged in user on many Unix-like platforms.
Used as the fossil user name if `FOSSIL_USER` is not specified. See
the discussion of Fossil Username below for a lot more detail.

`USERNAME`: Name of the logged in user on Windows platforms.
Used as the fossil user name if `FOSSIL_USER` is not specified. See
the discussion of Fossil Username below for a lot more detail.

`VISUAL`: Name the editor to use for check-in and stash comments.
Overrides the `EDITOR` environment variable. Overridden by the local
or global `editor` setting.



Notes on Related Values
-----------------------

### CGI and JSON Parameters


The JSON API implementation looks up many values in the first of
several places searched. This unifies the parameter handling logic,
allows the caller to choose whether to prefer URL parameters, request
headers, or the POST payload, and allows the `fossil json` command to
share most of the same logic as the `/json` API path. The search order
is a POST payload, GET/COOKIE/non-JSON POST, JSON POST, the system
environment.

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


### Error logging

If logging errors to a file, fossil will include the values of the
following environment variables in the error log entry if they are
defined: `HTTP_HOST`, `HTTP_USER_AGENT`, `PATH_INFO`, `QUERY_STRING`,
`REMOTE_ADDR`, `REQUEST_METHOD`, `REQUEST_URI`, and `SCRIPT_NAME`.



### Fossil Username

In absence of any explicit setting, fossil will use the same name you
logged in to your platform with, as the user name when interacting
with local and remote repositories. Note that only the name is shared,
fossil makes no attempt to share or leverage any platform's
authentication mechanisms or passwords.

When logging in to a repository, it tries a series of sources for the
user name, and the first non-blank name that succeeds is the logged in
user. The order is:

1.  The --user and -U command-line options.
2.  If running within an open checkout (the local database is open),
    check in its table of values stored per open checkout for the
    value stored by `fossil user default USERNAME`.
3.  The default user in the repository (setting `default-user`)
4.  The `FOSSIL_USER` environment variable.
5.  The `USER` environment variable.
6.  The `LOGNAME` environment variable.
7.  The `USERNAME` environment variable.
8.  Check if the user can be extracted from the remote URL, if
    there is a remote URL.

Items 2 and 3 are both set by `fossil user default USERNAME`, the
first within an open checkout, the second outside and using the `-R
REPOSITORY` option to identify the repository. Both cases require that
the named user be present in the repository when the default user is
assigned. Although the default user is internally stored as if it were
a setting named `default-user`, it is not accessible through
the `fossil set` command.

Items 5, 6, and 7 cover most of the names of an environment variable
set automatically by the platform with the name of the platform's
logged in user for use by programs. Historically, `USER` comes from
Unix System-V, `LOGNAME` from BSD, and `USERNAME` from Windows, but
many Linux distributions will set both `USER` and `LOGNAME` for broad
compatibility.

When creating a new repository, fossil needs a user name for the admin
user granted the "s" permission. But since fossil generally expects
that `fossil new` or `fossil clone` are used outside of any checkout
(especially when run for the first time without any checkouts at all
or the users's global settings database), it looks in a shorter list
of places for a non-blank name. In the special case of a clone,
`default-user` can be copied from the original, and so it can be set
in the clone even before any users have been created, and in that case
it will be the new admin user. If `default-user` is not set, then the
first found environment variable from the list `FOSSIL_USER`, `USER`,
`LOGNAME`, and `USERNAME`, is the user name. As a final fallback, if
none of those are set, then the default user name is "root".


### Configuration Database Location

Fossil keeps some information pertinent to each user in the user's
[configuration database file][configdb]. 
The configuration database file includes the global settings
and the list of repositories and checkouts used by `fossil all`.

The location of the configuration database file depends on the
operating system and on the existence of various environment
variables and/or files.  In brief, the configuration database is
usually:

  *  Traditional unix &rarr; "`$HOME/.fossil`"
  *  Windows &rarr; "`%LOCALAPPDATA%/_fossil`"
  *  [XDG-unix][xdg] &rarr; "`$HOME/.config/fossil.db`"

[xdg]: https://www.freedesktop.org/wiki/

See the [configuration database location
algorithm][configloc] discussion for full information.

### SQLite VFS to use

See [the SQLite documentation](http://www.sqlite.org/vfs.html) for an
explanation of what a VFS actually is and what it does.

If the default VFS underneath SQLite is not suitable, an alternative
can be selected with either the `--vfs VFSNAME` option or the
`FOSSIL_VFS` environment variable. The `--vfs` option takes
precedence.


### <a id="temp"></a>Temporary File Location

Fossil places some temporary files in the checkout directory. Most notably,
supporting files related to merge conflicts are placed in the same
folder as the merge result.

Other temporary files need a different home. The rules for choosing one are
complicated.

Fossil-specific code uses `FOSSIL_TEMP`, `TEMP`, and `TMP`, in that
order. Fossil’s own test suite prepends `FOSSIL_TEST_TEMP` to that list.

The underlying SQLite code uses several different path sets for its temp
files, depending on the platform type.

On Unix-like platforms, excepting Cygwin, SQLite first checks the
environment variables `SQLITE_TMPDIR` and `TMPDIR`, in that order. If
neither is defined, it falls back to a hard-coded list of paths:
`/var/tmp`, `/usr/tmp`, and `/tmp`. If all of that fails, it uses the
current working directory.

For Cygwin builds, SQLite instead uses the first defined variable in
this list: `SQLITE_TMPDIR`, `TMPDIR`, `TMP`, `TEMP`, and `USERPROFILE`.

For native Windows builds, SQLite simply calls the OS’s [`GetTempPath()`
API][gtp].  See that reference page for details.

[gtp]: https://msdn.microsoft.com/en-us/library/windows/desktop/aa364992%28v=vs.85%29.aspx



That said, it is not unusual for utilities on all platforms to assume
that `TEMP` or `TMP` point somewhere safe for temporary files.

If the identified temporary folder is not writable, then weird things
will happen on all platforms.


### Web browser

Occasionally, fossil wants to launch a web browser for the user, most
obviously as part of the `fossil ui` command. In that specific case,
the browser is launched pointing at the web server started by
`fossil ui` listening on a private TCP port.

On all platforms, if the local or global settings `web-browser` is
set, that is the command used to open a URL.

Otherwise, the specific actions vary by platform.

On Unix-like platforms other than Apple's, it looks for the first
program from the list `xdg-open`, `gnome-open`, `firefox`, and
`google-chrome` that it can find on the `PATH`.

On Apple platforms, it assumes that `open` is the command to open a
URL in the user's configured default browser.

On Windows platforms, it assumes that `start` is the command to open
a URL in the user's configured default browser.

[configdb]: ./tech_overview.wiki#configdb
[configloc]: ./tech_overview.wiki#configloc
[cgictlfile]: ./cgi.wiki
