# File Name Glob Patterns

A [glob pattern][glob] is a text expression that matches one or more
file names using wildcards familiar to most users of a command line.
For example, `*` is a glob that matches any name at all, and
`Readme.txt` is a glob that matches exactly one file. For purposes of
Fossil's globs, a complete path name is just a string,
and the globs do not apply any special meaning to the directory part
of the name. Thus, the glob `*` matches any name, including any
directory prefix, and `*/*` matches a name with _one or more_
directory components.

A glob should not be confused with a [regular expression][regexp] (RE)
even though they use some of the same special characters for similar
purposes. [They are not fully compatible][greinc] pattern
matching languages. Fossil uses globs when matching file names with the
settings described in this document, not REs.

[glob]:   https://en.wikipedia.org/wiki/Glob_(programming)
[greinc]: https://unix.stackexchange.com/a/57958/138
[regexp]: https://en.wikipedia.org/wiki/Regular_expression

[Fossil’s `*-glob` settings](#settings) hold one or more patterns to cause Fossil to
give matching named files special treatment.  Glob patterns are also
accepted in options to certain commands and as query parameters to
certain Fossil UI web pages. For consistency, settings such as
`empty-dirs` are parsed as a glob even though they aren’t then *applied*
as a glob since it allows [the same syntax rules](#syntax) to apply.

Where Fossil also accepts globs in commands, this handling may interact
with your OS’s command shell or its C runtime system, because they may
have their own glob pattern handling. We will detail such interactions
below.


## <a id="syntax"></a>Syntax

Where Fossil accepts glob patterns, it will usually accept a *list* of
individual patterns separated from the others by whitespace or commas.

The parser allows whitespace and commas in a pattern by quoting _the
entire pattern_ with either single or double quotation marks. Internal
quotation marks are treated literally. Moreover, a pattern that begins
with a quote mark ends when the first instance of the same mark occurs,
_not_ at a whitespace or comma. Thus, this:

    "foo bar"qux

…constitutes _two_ patterns rather than one with an embedded space, in
contravention of normal shell quoting rules.

A list matches a file when any pattern in that list matches.

A pattern must consume and
match the *entire* file name to succeed. Partial matches are failed matches.

Most characters in a glob pattern consume a single character of the file
name and must match it exactly. For instance, “a” in a glob simply
matches the letter “a” in the file name unless it is inside a special
character sequence.

Other characters have special meaning, and they may include otherwise
normal characters to give them special meaning:

:Pattern |:Effect
---------------------------------------------------------------------
`*`      | Matches any sequence of zero or more characters
`?`      | Matches exactly one character
`[...]`  | Matches one character from the enclosed list of characters
`[^...]` | Matches one character *not* in the enclosed list

Note that unlike [POSIX globs][pg], these special characters and
sequences are allowed to match `/` directory separators as well as the
initial `.` in the name of a hidden file or directory. This is because
Fossil file names are stored as complete path names. The distinction
between file name and directory name is “underneath” Fossil in this sense.

[pg]: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_13

The bracket expressions above require some additional explanation:

 *  A range of characters may be specified with `-`, so `[a-f]` matches
    exactly the same characters as `[abcdef]`. Ranges reflect Unicode
    code points without any locale-specific collation sequence.
    Therefore, this particular sequence never matches the Unicode
    pre-composed character `é`, for example. (U+00E9)

 *  This dependence on character/code point ordering may have other
    effects to surprise you. For example, the glob `[A-z]` not only
    matches upper and lowercase ASCII letters, it also matches several
    punctuation characters placed between `Z` and `a` in both ASCII and
    Unicode: `[`, `\`, `]`, `^`, `_`, and <tt>\`</tt>.

 *  You may include a literal `-` in a list by placing it last, just
    before the `]`.

 *  You may include a literal `]` in a list by making the first
    character after the `[` or `[^`. At any other place, `]` ends the list.

 *  You may include a literal `^` in a list by placing it anywhere
    except after the opening `[`.

 *  Beware that a range must be specified from low value to high
    value: `[z-a]` does not match any character at all, preventing the
    entire glob from matching.

Some examples of character lists:

:Pattern |:Effect
---------------------------------------------------------------------
`[a-d]`  | Matches any one of `a`, `b`, `c`, or `d` but not `ä`
`[^a-d]` | Matches exactly one character other than `a`, `b`, `c`, or `d`
`[0-9a-fA-F]` | Matches exactly one hexadecimal digit
`[a-]`   | Matches either `a` or `-`
`[][]`   | Matches either `]` or `[`
`[^]]`   | Matches exactly one character other than `]`
`[]^]`   | Matches either `]` or `^`
`[^-]`   | Matches exactly one character other than `-`

White space means the specific ASCII characters TAB, LF, VT, FF, CR,
and SPACE.  Note that this does not include any of the many additional
spacing characters available in Unicode such as
U+00A0, NO-BREAK SPACE.

Because both LF and CR are white space and leading and trailing spaces
are stripped from each glob in a list, a list of globs may be broken
into lines between globs when the list is stored in a file, as for a
versioned setting.

Note that 'single quotes' and "double quotes" are the ASCII straight
quote characters, not any of the other quotation marks provided in
Unicode and specifically not the "curly" quotes preferred by
typesetters and word processors.


## File Names to Match

Before it is compared to a glob pattern, each file name is transformed
to a canonical form:

  *  all directory separators are changed to `/`
  *  redundant slashes are removed
  *  all `.` path components are removed
  *  all `..` path components are resolved

(There are additional details we are ignoring here, but they cover rare
edge cases and follow the principle of least surprise.)

The glob must match the *entire* canonical file name to be considered a
match.

The goal is to have a name that is the simplest possible for each
particular file, and that will be the same regardless of the platform
you run Fossil on. This is important when you have a repository cloned
from multiple platforms and have globs in versioned settings: you want
those settings to be interpreted the same way everywhere.

Beware, however, that all glob matching in Fossil is case sensitive
regardless of host platform and file system. This will not be a surprise
on POSIX platforms where file names are usually treated case
sensitively. However, most Windows file systems are case preserving but
case insensitive. That is, on Windows, the names `ReadMe` and `README`
are usually names of the same file. The same is true in other cases,
such as by default on macOS file systems and in the file system drivers
for Windows file systems running on non-Windows systems. (e.g. exfat on
Linux.) Therefore, write your Fossil glob patterns to match the name of
the file as checked into the repository.

Some example cases:

:Pattern     |:Effect
--------------------------------------------------------------------------------
`README`     | Matches only a file named `README` in the root of the tree. It does not match a file named `src/README` because it does not include any characters that consume (and match) the `src/` part.
`*/README`   | Matches `src/README`. Unlike Unix file globs, it also matches `src/library/README`. However it does not match the file `README` in the root of the tree.
`*README`    | Matches `src/README` as well as the file `README` in the root of the tree as well as `foo/bar/README` or any other file named `README` in the tree. However, it also matches `A-DIFFERENT-README` and `src/DO-NOT-README`, or any other file whose name ends with `README`.
`src/README` | Matches `src\README` on Windows because all directory separators are rewritten as `/` in the canonical name before the glob is matched. This makes it much easier to write globs that work on both Unix and Windows.
`*.[ch]`     | Matches every C source or header file in the tree at the root or at any depth. Again, this is (deliberately) different from Unix file globs and Windows wild cards.


## Where Globs are Used

### <a id="settings"></a>Settings that are Globs

These settings are all lists of glob patterns:

:Setting        |:Description
--------------------------------------------------------------------------------
`binary-glob`   | Files that should be treated as binary files for committing and merging purposes
`clean-glob`    | Files that the [`clean`][] command will delete without prompting or allowing undo
`crlf-glob`     | Files in which it is okay to have `CR`, `CR`+`LF` or mixed line endings.  Set to "`*`" to disable CR+LF checking
`crnl-glob`     | Alias for the `crlf-glob` setting
`encoding-glob` | Files that the [`commit`][] command will ignore when issuing warnings about text files that may use another encoding than ASCII or UTF-8.  Set to "`*`" to disable encoding checking
`ignore-glob`   | Files that the [`add`][], [`addremove`][], [`clean`][], and [`extras`][] commands will ignore
`keep-glob`     | Files that the [`clean`][] command will keep

All may be [versioned, local, or global](settings.wiki). Use `fossil
settings` to manage local and global settings, or a file in the
repository's `.fossil-settings/` folder at the root of the tree named
for each for versioned setting.

Using versioned settings for these not only has the advantage that
they are tracked in the repository just like the rest of your project,
but you can more easily keep longer lists of more complicated glob
patterns than would be practical in either local or global settings.

The `ignore-glob` is an example of one setting that frequently grows
to be an elaborate list of files that should be ignored by most
commands. This is especially true when one (or more) IDEs are used in
a project because each IDE has its own ideas of how and where to cache
information that speeds up its browsing and building tasks but which
need not be preserved in your project's history.

Although the `empty-dirs` setting is not a list of glob patterns as
such, it is *parsed* that way for consistency among the settings,
allowing [the list parsing rules above](#syntax) to apply.


### <a id="commands"></a>Commands that Refer to Globs

Many of the commands that respect the settings containing globs have
options to override some or all of the settings. These options are
usually named to correspond to the setting they override, such as
`--ignore` to override the `ignore-glob` setting. These commands are:

 *  [`add`][]
 *  [`addremove`][]
 *  [`changes`][]
 *  [`clean`][]
 *  [`commit`][]
 *  [`extras`][]
 *  [`merge`][]
 *  [`settings`][]
 *  [`status`][]
 *  [`touch`][]
 *  [`unset`][]

The commands [`tarball`][] and [`zip`][] produce compressed archives of a
specific checkin. They may be further restricted by options that
specify glob patterns that name files to include or exclude rather
than archiving the entire checkin.

The commands [`http`][], [`cgi`][], [`server`][], and [`ui`][] that
implement or support with web servers provide a mechanism to name some
files to serve with static content where a list of glob patterns
specifies what content may be served.

[`add`]:       /help?cmd=add
[`addremove`]: /help?cmd=addremove
[`changes`]:   /help?cmd=changes
[`clean`]:     /help?cmd=clean
[`commit`]:    /help?cmd=commit
[`extras`]:    /help?cmd=extras
[`merge`]:     /help?cmd=merge
[`settings`]:  /help?cmd=settings
[`status`]:    /help?cmd=status
[`touch`]:     /help?cmd=touch
[`unset`]:     /help?cmd=unset

[`tarball`]:   /help?cmd=tarball
[`zip`]:       /help?cmd=zip

[`http`]:      /help?cmd=http
[`cgi`]:       /help?cmd=cgi
[`server`]:    /help?cmd=server
[`ui`]:        /help?cmd=ui


### Web Pages that Refer to Globs

The [`/timeline`][] page supports the query parameter `chng=GLOBLIST` that
names a list of glob patterns defining which files to focus the
timeline on. It also has the query parameters `t=TAG` and `r=TAG` that
names a tag to focus on, which can be configured with `ms=STYLE` to
use a glob pattern to match tag names instead of the default exact
match or a couple of other comparison styles.

The pages [`/tarball`][] and [`/zip`][] generate compressed archives
of a specific checkin. They may be further restricted by query
parameters that specify glob patterns that name files to include or
exclude rather than taking the entire checkin.

[`/timeline`]: /help?cmd=/timeline
[`/tarball`]:  /help?cmd=/tarball
[`/zip`]:      /help?cmd=/zip


## Platform Quirks

Fossil glob patterns are based on the glob pattern feature of POSIX
shells. Fossil glob patterns also have a quoting mechanism, discussed
[above](#syntax). Because other parts of your operating system may interpret glob
patterns and quotes separately from Fossil, it is often difficult to
give glob patterns correctly to Fossil on the command line. Quotes and
special characters in glob patterns are likely to be interpreted when
given as part of a `fossil` command, causing unexpected behavior.

These problems do not affect [versioned settings files](settings.wiki)
or Admin &rarr; Settings in Fossil UI. Consequently, it is better to
set long-term `*-glob` settings via these methods than to use `fossil
settings` commands.

That advice does not help you when you are giving one-off glob patterns
in `fossil` commands. The remainder of this section gives remedies and
workarounds for these problems.


### <a id="posix"></a>POSIX Systems

If you are using Fossil on a system with a POSIX-compatible shell
&mdash; Linux, macOS, the BSDs, Unix, Cygwin, WSL etc. &mdash; the shell
may expand the glob patterns before passing the result to the `fossil`
executable.

Sometimes this is exactly what you want.  Consider this command for
example:

    $ fossil add RE*

If you give that command in a directory containing `README.txt` and
`RELEASE-NOTES.txt`, the shell will expand the command to:

    $ fossil add README.txt RELEASE-NOTES.txt

…which is compatible with the `fossil add` command's argument list,
which allows multiple files.

Now consider what happens instead if you say:

    $ fossil add --ignore RE* src/*.c

This *does not* do what you want because the shell will expand both `RE*`
and `src/*.c`, causing one of the two files matching the `RE*` glob
pattern to be ignored and the other to be added to the repository. You
need to say this in that case:

    $ fossil add --ignore 'RE*' src/*.c

The single quotes force a POSIX shell to pass the `RE*` glob pattern
through to Fossil untouched, which will do its own glob pattern
matching. There are other methods of quoting a glob pattern or escaping
its special characters; see your shell's manual.

Beware that Fossil's `--ignore` option does not override explicit file
mentions:

    $ fossil add --ignore 'REALLY SECRET STUFF.txt' RE*

You might think that would add everything beginning with `RE` *except*
for `REALLY SECRET STUFF.txt`, but when a file is both given
explicitly to Fossil and also matches an ignore rule, Fossil asks what
you want to do with it in the default case; and it does not even ask
if you gave the `-f` or `--force` option along with `--ignore`.

The spaces in the ignored file name above bring us to another point:
such file names must be quoted in Fossil glob patterns, lest Fossil
interpret it as multiple glob patterns, but the shell interprets
quotation marks itself.

One way to fix both this and the previous problem is:

    $ fossil add --ignore "'REALLY SECRET STUFF.txt'" READ*

The nested quotation marks cause the inner set to be passed through to
Fossil, and the more specific glob pattern at the end &mdash; that is,
`READ*` vs `RE*` &mdash; avoids a conflict between explicitly-listed
files and `--ignore` rules in the `fossil add` command.

Another solution would be to use shell escaping instead of nested
quoting:

    $ fossil add --ignore "\"REALLY SECRET STUFF.txt\"" READ*

It bears repeating that the two glob patterns here are not interpreted
the same way when running this command from a *subdirectory* of the top
checkout directory as when running it at the top of the checkout tree.
If these files were in a subdirectory of the checkout tree called `doc`
and that was your current working directory, the command would instead
have to be:

    $ fossil add --ignore "'doc/REALLY SECRET STUFF.txt'" READ*

The Fossil glob pattern still needs the `doc/` prefix because
Fossil always interprets glob patterns from the base of the checkout
directory, not from the current working directory as POSIX shells do.

When in doubt, use `fossil status` after running commands like the
above to make sure the right set of files were scheduled for insertion
into the repository before checking the changes in. You never want to
accidentally check something like a password, an API key, or the
private half of a public cryptographic key into Fossil repository that
can be read by people who should not have such secrets.


### <a id="windows"></a>Windows

Before we get into Windows-specific details here, beware that this
section does not apply to the several Microsoft Windows extensions that
provide POSIX semantics to Windows, for which you want to use the advice
in [the POSIX section above](#posix) instead:

  *  the ancient and rarely-used [Microsoft POSIX subsystem][mps];
  *  its now-discontinued replacement feature, [Services for Unix][sfu]; or
  *  their modern replacement, the [Windows Subsystem for Linux][wsl]

[mps]: https://en.wikipedia.org/wiki/Microsoft_POSIX_subsystem
[sfu]: https://en.wikipedia.org/wiki/Windows_Services_for_UNIX
[wsl]: https://en.wikipedia.org/wiki/Windows_Subsystem_for_Linux

(The latter is sometimes incorrectly called "Bash on Windows" or "Ubuntu
on Windows," but the feature provides much more than just Bash or Ubuntu
for Windows.)

Neither standard Windows command shell &mdash; `cmd.exe` or PowerShell
&mdash; expands glob patterns the way POSIX shells do. Windows command
shells rely on the command itself to do the glob pattern expansion. The
way this works depends on several factors:

 *  the version of Windows you are using
 *  which OS upgrades have been applied to it
 *  the compiler that built your Fossil executable
 *  whether you are running the command interactively
 *  whether the command is built against a runtime system that does this
    at all
 *  whether the Fossil command is being run from a file named `*.BAT` vs
    being named `*.CMD`

Usually (but not always!) the C runtime library that your `fossil.exe`
executable is built against does this glob expansion on Windows so the
program proper does not have to. This may then interact with the way the
Windows command shell you’re using handles argument quoting. Because of
these differences, it is common to find perfectly valid Fossil command
examples that were written and tested on a POSIX system which then fail
when tried on Windows.

The most common problem is figuring out how to get a glob pattern passed
on the command line into `fossil.exe` without it being expanded by the C
runtime library that your particular Fossil executable is linked to,
which tries to act like [the POSIX systems described above](#posix). Windows is
not strongly governed by POSIX, so it has not historically hewed closely
to its strictures.

For example, consider how you would set `crlf-glob` to `*` in order to
get normal Windows text files with CR+LF line endings past Fossil's
"looks like a binary file" check. The na&iuml;ve approach will not work:

    C:\...> fossil setting crlf-glob *

The C runtime library will expand that to the list of all files in the
current directory, which will probably cause a Fossil error because
Fossil expects either nothing or option flags after the setting's new
value, not a list of file names. (To be fair, the same thing will happen
on POSIX systems, only at the shell level, before `.../bin/fossil` even
gets run by the shell.)

Let's try again:

    C:\...> fossil setting crlf-glob '*'

Quoting the argument like that will work reliably on POSIX, but it may
or may not work on Windows. If your Windows command shell interprets the
quotes, it means `fossil.exe` will see only the bare `*` so the C
runtime library it is linked to will likely expand the list of files in
the current directory before the `setting` command gets a chance to
parse the command line arguments, causing the same failure as above.
This alternative only works if you’re using a Windows command shell that
passes the quotes through to the executable *and* you have linked Fossil
to a C runtime library that interprets the quotes properly itself,
resulting in a bare `*` getting clear down to Fossil’s `setting` command
parser.

An approach that *will* work reliably is:

    C:\...> echo * | fossil setting crlf-glob --args -

This works because the built-in Windows command `echo` does not expand its
arguments, and the `--args -` option makes Fossil read further command
arguments from its standard input, which is connected to the output
of `echo` by the pipe. (`-` is a common Unix convention meaning
"standard input," which Fossil obeys.) A [batch script][fng.cmd] to automate this trick was
posted on the now-inactive Fossil Mailing List.

[fng.cmd]: https://www.mail-archive.com/fossil-users@lists.fossil-scm.org/msg25099.html

(Ironically, this method will *not* work on POSIX systems because it is
not up to the command to expand globs. The shell will expand the `*` in
the `echo` command, so the list of file names will be passed to the
`fossil` standard input, just as with the first example above!)

Another (usually) correct approach which will work on both Windows and
POSIX systems:

    C:\...> fossil setting crlf-glob *,

This works because the trailing comma prevents the glob pattern from
matching any files, unless you happen to have files named with a
trailing comma in the current directory. If the pattern matches no
files, it is passed into Fossil's `main()` function as-is by the C
runtime system. Since Fossil uses commas to separate multiple glob
patterns, this means "all files from the root of the Fossil checkout
directory downward and nothing else," which is of course equivalent to
"all managed files in this repository," our original goal.


## Experimenting

To preview the effects of command line glob pattern expansion for
various glob patterns (unquoted, quoted, comma-terminated), for any
combination of command shell, OS, C run time, and Fossil version,
precede the command you want to test with [`test-echo`][] like so:

    $ fossil test-echo setting crlf-glob "*"
    C:\> echo * | fossil test-echo setting crlf-glob --args -

The [`test-glob`][] command is also handy to test if a string
matches a glob pattern.

[`test-echo`]: /help?cmd=test-echo
[`test-glob`]: /help?cmd=test-glob


## Converting `.gitignore` to `ignore-glob`

Many other version control systems handle the specific case of
ignoring certain files differently from Fossil: they have you create
individual "ignore" files in each folder, which specify things ignored
in that folder and below. Usually some form of glob patterns are used
in those files, but the details differ from Fossil.

In many simple cases, you can just store a top level "ignore" file in
`.fossil-settings/ignore-glob`. But as usual, there will be lots of
edge cases.

[Git has a rich collection of ignore files][gitignore] which
accumulate rules that affect the current command. There are global
files, per-user files, per workspace unmanaged files, and fully
version controlled files. Some of the files used have no set name, but
are called out in configuration files.

[gitignore]: https://git-scm.com/docs/gitignore

In contrast, Fossil has a global setting and a local setting, but the local setting
overrides the global rather than extending it. Similarly, a Fossil
command's `--ignore` option replaces the `ignore-glob` setting rather
than extending it.

With that in mind, translating a `.gitignore` file into
`.fossil-settings/ignore-glob` may be possible in many cases. Here are
some of features of `.gitignore` and comments on how they relate to
Fossil:

 *  "A blank line matches no files...": same in Fossil.
 *  "A line starting with # serves as a comment...": same in Fossil, including
    the possibility of escaping an initial `#` with a backslash to allow globs
    beginning with a hash.
 *  "Trailing spaces are ignored unless they are quoted..." is similar
    in Fossil. All whitespace before and after a glob is trimmed in
    Fossil unless quoted with single or double quotes. Git uses
    backslash quoting instead, which Fossil does not.
 *  "An optional prefix "!" which negates the pattern...": not in
    Fossil.
 *  Git's globs are relative to the location of the `.gitignore` file:
    Fossil's globs are relative to the root of the workspace.
 *  Git's globs and Fossil's globs treat directory separators
    differently. Git includes a notation for zero or more directories
    that is not needed in Fossil.

### Example

In a project with source and documentation:

    work
      +-- doc
      +-- src

The file `doc/.gitignore` might contain:

    # Finished documents by pandoc via LaTeX
    *.pdf
    # Intermediate files
    *.tex
    *.toc
    *.log
    *.out
    *.tmp

Entries in `.fossil-settings/ignore-glob` with similar effect, also
limited to the `doc` folder:

    doc/*.pdf
    doc/*.tex, doc/*.toc, doc/*.log, doc/*.out, doc/*.tmp





## Implementation and References

The implementation of the Fossil-specific glob pattern handling is here:

:File            |:Description
--------------------------------------------------------------------------------
[`src/glob.c`][] | pattern list loading, parsing, and generic matching code
[`src/file.c`][] | application of glob patterns to file names

[`src/glob.c`]: https://fossil-scm.org/home/file/src/glob.c
[`src/file.c`]: https://fossil-scm.org/home/file/src/file.c

See the [Adding Features to Fossil][aff] document for broader details
about finding and working with such code.

The actual pattern matching leverages the `GLOB` operator in SQLite, so
you may find [its documentation][gdoc], [source code][gsrc] and [test
harness][gtst] helpful.

[aff]:  ./adding_code.wiki
[gdoc]: https://sqlite.org/lang_expr.html#like
[gsrc]: https://www.sqlite.org/src/artifact?name=9d52522cc8ae7f5c&ln=570-768
[gtst]: https://www.sqlite.org/src/artifact?name=66a2c9ac34f74f03&ln=586-673
