# File Name Glob Patterns


A [glob pattern][glob] is a text expression that matches one or more
file names using wild cards familiar to most users of a command line.
For example, `*` is a glob that matches any name at all and
`Readme.txt` is a glob that matches exactly one file.

Note that although both are notations for describing patterns in text,
glob patterns are not the same thing as a [regular expression or
regexp][regexp].

[glob]: https://en.wikipedia.org/wiki/Glob_(programming) (Wikipedia)
[regexp]: https://en.wikipedia.org/wiki/Regular_expression


A number of fossil setting values hold one or more file glob patterns
that will identify files needing special treatment.  Glob patterns are
also accepted in options to certain commands as well as query
parameters to certain pages.

In many cases more than one glob may be specified in a setting,
option, or query parameter by listing multiple globs separated by a
comma or white space.

Of course, many fossil commands also accept lists of files to act on,
and those also may be specified with globs. Although those glob
patterns are similar to what is described here, they are not defined
by fossil, but rather by the conventions of the operating system in
use.


## Syntax

A list of glob patterns is simply one or more glob patterns separated
by white space or commas. If a glob must contain white spaces or
commas, it can be quoted with either single or double quotation marks.
A list is said to match if any one (or more) globs in the list
matches.

A glob pattern is a collection of characters compared to a target
text, usually a file name. The whole glob is said to match if it
successfully consumes and matches the entire target text. Glob
patterns are made up of ordinary characters and special characters.

Ordinary characters consume a single character of the target and must
match it exactly.

Special characters (and special character sequences) consume zero or
more characters from the target and describe what matches. The special
characters (and sequences) are:

:Pattern |:Effect
---------------------------------------------------------------------
`*`      | Matches any sequence of zero or more characters
`?`      | Matches exactly one character
`[...]`  | Matches one character from the enclosed list of characters
`[^...]` | Matches one character not in the enclosed list

Special character sequences have some additional features:

 *  A range of characters may be specified with `-`, so `[a-d]` matches
    exactly the same characters as `[abcd]`. Ranges reflect Unicode
    code points without any locale-specific collation sequence.
 *  Include `-` in a list by placing it last, just before the `]`.
 *  Include `]` in a list by making the first character after the `[` or
    `[^`. At any other place, `]` ends the list.
 *  Include `^` in a list by placing anywhere except first after the
    `[`.
 *  Beware that ranges in lists may include more than you expect:
    `[A-z]` Matches `A` and `Z`, but also matches `a` and some less
    obvious characters such as `[`, `\`, and `]` with code point
    values between `Z` and `a`.
 *  Beware that a range must be specified from low value to high
    value: `[z-a]` does not match any character at all, preventing the
    entire glob from matching.
 *  Note that unlike typical Unix shell globs, wildcards (`*`, `?`,
    and character lists) are allowed to match `/` directory
    separators as well as the initial `.` in the name of a hidden
    file or directory.

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
spacing characters available in Unicode, and specifically does not
include U+00A0 NO-BREAK SPACE.

Because both LF and CR are white space and leading and trailing spaces
are stripped from each glob in a list, a list of globs may be broken
into lines between globs when the list is stored in a file (as for a
versioned setting).

Similarly 'single quotes' and "double quotes" are the ASCII straight
quote characters, not any of the other quotation marks provided in
Unicode and specifically not the "curly" quotes preferred by
typesetters and word processors.


## File Names to Match

Before it is compared to a glob pattern, each file name is transformed
to a canonical form. The glob must match the entire canonical file
name to be considered a match.

The canonical name of a file has all directory separators changed to
`/`, redundant slashes are removed, all `.` path components are
removed, and all `..` path components are resolved. (There are
additional details we are ignoring here, but they cover rare edge
cases and also follow the principle of least surprise.)

The goal is to have a name that is the simplest possible for each
particular file, and that will be the same on Windows, Unix, and any
other platform where fossil is run.

Beware, however, that all glob matching is case sensitive. This will
not be a surprise on Unix where all file names are also case
sensitive. However, most Windows file systems are case preserving and
case insensitive. That is, on Windows, the names `ReadMe` and `README`
are names of the same file; on Unix they are different files.

Some example cases:

:Pattern     |:Effect
--------------------------------------------------------------------------------
`README`     | Matches only a file named `README` in the root of the tree. It does not match a file named `src/README` because it does not include any characters that consume (and match) the `src/` part.
`*/README`   | Matches `src/README`. Unlike Unix file globs, it also matches `src/library/README`. However it does not match the file `README` in the root of the tree.
`*README`    | Matches `src/README` as well as the file `README` in the root of the tree as well as `foo/bar/README` or any other file named `README` in the tree. However, it also matches `A-DIFFERENT-README` and `src/DO-NOT-README`, or any other file whose name ends with `README`.
`src/README` | Matches `src\README` on Windows because all directory separators are rewritten as `/` in the canonical name before the glob is matched. This makes it much easier to write globs that work on both Unix and Windows.
`*.[ch]`     | Matches every C source or header file in the tree at the root or at any depth. Again, this is (deliberately) different from Unix file globs and Windows wild cards.

## Where Globs are Used

### Settings that are Globs

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


### Commands that Refer to Globs

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
 *  [`unset`][]

The commands [`tarball`][] and [`zip`][] produce compressed archives of a
specific checkin. They may be further restricted by options that
specify glob patterns that name files to include or exclude rather
than archiving the entire checkin.

The commands [`http`][], [`cgi`][], [`server`][], and [`ui`][] that
implement or support with web servers provide a mechanism to name some
files to serve with static content where a list of glob patterns
specifies what content may be served.

[`add`]: /help?cmd=add
[`addremove`]: /help?cmd=addremove
[`changes`]: /help?cmd=changes
[`clean`]: /help?cmd=clean
[`commit`]: /help?cmd=commit
[`extras`]: /help?cmd=extras
[`merge`]: /help?cmd=merge
[`settings`]: /help?cmd=settings
[`status`]: /help?cmd=status
[`unset`]: /help?cmd=unset

[`tarball`]: /help?cmd=tarball
[`zip`]: /help?cmd=zip

[`http`]: /help?cmd=http
[`cgi`]: /help?cmd=cgi
[`server`]: /help?cmd=server
[`ui`]: /help?cmd=ui


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
[`/tarball`]: /help?cmd=/tarball
[`/zip`]: /help?cmd=/zip


## Platform Quirks

Fossil glob patterns are based on the glob pattern feature of POSIX
shells. Fossil glob patterns also have a quoting mechanism, discussed
above. Because other parts of your operating system may interpret glob
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


## POSIX Systems

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
and that was your current working directory, the command would have to
be:

    $ fossil add --ignore "'doc/REALLY SECRET STUFF.txt'" READ*

instead. The Fossil glob pattern still needs the `doc/` prefix because
Fossil always interprets glob patterns from the base of the checkout
directory, not from the current working directory as POSIX shells do.

When in doubt, use `fossil status` after running commands like the
above to make sure the right set of files were scheduled for insertion
into the repository before checking the changes in. You never want to
accidentally check something like a password, an API key, or the
private half of a public cryptographic key into Fossil repository that
can be read by people who should not have such secrets.


## Windows

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

These factors also affect how a program like `fossil.exe` interprets
quotation marks on its command line.

The fifth item above does not apply to `fossil.exe` when built with
typical tool chains, but we will see an example below where the exception
applies in a way that affects how Fossil interprets the glob pattern.

The most common problem is figuring out how to get a glob pattern passed
on the command line into `fossil.exe` without it being expanded by the C
runtime library that your particular Fossil executable is linked to,
which tries to act like the POSIX systems described above. Windows is
not strongly governed by POSIX, so it has not historically hewed closely
to its strictures.

(This section does not cover the [Microsoft POSIX
subsystem](https://en.wikipedia.org/wiki/Microsoft_POSIX_subsystem),
Windows' obsolete [Services for Unix
3.*x*](https://en.wikipedia.org/wiki/Windows_Services_for_UNIX) feature,
or the [Windows Subsystem for
Linux](https://en.wikipedia.org/wiki/Windows_Subsystem_for_Linux). (The
latter is sometimes incorrectly called "Bash on Windows" or "Ubuntu on
Windows.") See the POSIX Systems section above for those cases.)

For example, consider how you would set `crlf-glob` to `*` in order to
disable Fossil's "looks like a binary file" checks. The na&iuml;ve
approach will not work:

    C:\...> fossil setting crlf-glob *

The C runtime library will expand that to the list of all files in the
current directory, which will probably cause a Fossil error because
Fossil expects either nothing or option flags after the setting's new
value.

Let's try again:

    C:\...> fossil setting crlf-glob '*'

That may or may not work. Either `'*'` or `*` needs to be passed through
to Fossil untouched for this to do what you expect, which may or may not
happen, depending on the factors listed above.

An approach that *will* work reliably is:

    C:\...> echo * | fossil setting crlf-glob --args -

This works because the built-in command `echo` does not expand its
arguments, and the `--args -` option makes it read further command
arguments from Fossil's standard input, which is connected to the output
of `echo` by the pipe. (`-` is a common Unix convention meaning
"standard input.")

Another (usually) correct approach is:

    C:\...> fossil setting crlf-glob *,

This works because the trailing comma prevents the command shell from
matching any files, unless you happen to have files named with a
trailing comma in the current directory. If the pattern matches no
files, it is passed into Fossil's `main()` function as-is by the C
runtime system. Since Fossil uses commas to separate multiple glob
patterns, this means "all files at the root of the Fossil checkout
directory and nothing else."


## Converting `.gitignore` to `ignore-glob`

Many other version control systems handle the specific case of
ignoring certain files differently from fossil: they have you create
individual "ignore" files in each folder, which specify things ignored
in that folder and below. Usually some form of glob patterns are used
in those files, but the details differ from fossil.

In many simple cases, you can just store a top level "ignore" file in
`.fossil-settings/ignore-glob`. But as usual, there will be lots of
edge cases.

[Git has a rich collection of ignore files][gitignore] which
accumulate rules that affect the current command. There are global
files, per-user files, per workspace unmanaged files, and fully
version controlled files. Some of the files used have no set name, but
are called out in configuration files.

[gitignore]: https://git-scm.com/docs/gitignore

In contrast, fossil has a global setting and a local setting, but the local setting
overrides the global rather than extending it. Similarly, a fossil
command's `--ignore` option replaces the `ignore-glob` setting rather
than extending it.

With that in mind, translating a `.gitignore` file into
`.fossil-settings/ignore-glob` may be possible in many cases. Here are
some of features of `.gitignore` and comments on how they relate to
fossil:

 *  "A blank line matches no files..." is the same in fossil.
 *  "A line starting with # serves as a comment...." not in fossil.
 *  "Trailing spaces are ignored unless they are quoted..." is similar
    in fossil. All whitespace before and after a glob is trimmed in
    fossil unless quoted with single or double quotes. Git uses
    backslash quoting instead, which fossil does not.
 *  "An optional prefix "!" which negates the pattern..." not in
    fossil.
 *  Git's globs are relative to the location of the `.gitignore` file;
    fossil's globs are relative to the root of the workspace.
 *  Git's globs and fossil's globs treat directory separators
    differently. Git includes a notation for zero or more directories
    that is not needed in fossil.

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

Most of the implementation of glob pattern handling in fossil is found
`glob.c`, `file.c`, and each individual command and web page that uses
a glob pattern. Find commands and pages in the fossil sources by
looking for comments like `COMMAND: add` or `WEBPAGE: timeline` in
front of the function that implements the command or page in files
`src/*.c`. (Fossil's build system creates the tables used to dispatch
commands at build time by searching the sources for those comments.) A
few starting points:

:File            |:Description
--------------------------------------------------------------------------------
[`src/glob.c`][] | Implementation of glob pattern list loading, parsing, and matching.
[`src/file.c`][] | Implementation of various kinds of canonical names of a file.

[`src/glob.c`]: https://www.fossil-scm.org/index.html/file/src/glob.c
[`src/file.c`]: https://www.fossil-scm.org/index.html/file/src/file.c

The actual pattern matching is implemented in SQL, so the
documentation for `GLOB` and the other string matching operators in
[SQLite] (https://sqlite.org/lang_expr.html#like) is useful. Of
course, the SQLite [source code]
(https://www.sqlite.org/src/artifact?name=9d52522cc8ae7f5c&ln=570-768)
and [test harnesses]
(https://www.sqlite.org/src/artifact?name=66a2c9ac34f74f03&ln=586-673)
also make entertaining reading.
