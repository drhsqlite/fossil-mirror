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

    *       Matches any sequence of zero or more characters.
    ?       Matches exactly one character.
    [...]   Matches one character from the enclosed list of characters.
    [^...]  Matches one character not in the enclosed list.

Special character sequences have some additional features: 

 *  A range of characters may be specified with `-`, so `[a-d]` matches
    exactly the same characters as `[abcd]`. Ranges reflect Unicode
    code points without any locale-specific collation sequence.

 *  Include `-` in a list by placing it last, just before the `]`.

 *  Include `]` in a list by making the first character after the `[` or
    `[^`. At any other place, `]` ends the list. 

 *  Include `^` in a list by placing anywhere except first after the
    `[`.

 *  Some examples of character lists: 
    `[a-d]` Matches any one of `a`, `b`, `c`, or `d` but not `ä`;
    `[^a-d]` Matches exactly one character other than `a`, `b`, `c`,
    or `d`; 
    `[0-9a-fA-F]` Matches exactly one hexadecimal digit;
    `[a-]` Matches either `a` or `-`;
    `[][]` Matches either `]` or `[`;
    `[^]]` Matches exactly one character other than `]`;
    `[]^]` Matches either `]` or `^`; and
    `[^-]` Matches exactly one character other than `-`.

    Beware that ranges in lists may include more than you expect: 
    `[A-z]` Matches `A` and `Z`, but also matches `a` and some less
    obvious characters such as `[`, `\`, and `]` with code point
    values between `Z` and `a`.

    Beware that a range must be specified from low value to high
    value: `[z-a]` does not match any character at all, preventing the
    entire glob from matching.

 *  Note that unlike typical Unix shell globs, wildcards (`*`, `?`,
    and character lists) are allowed to match `/` directory
    separators as well as the initial `.` in the name of a hidden
    file or directory.


White space means the ASCII characters TAB, LF, VT, FF, CR, and SPACE.
Note that this does not include any of the many additional spacing
characters available in Unicode, and specifically does not include
U+00A0 NO-BREAK SPACE. 

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
additional details we won’t go into here.)

The goal is a name that is the simplest possible for each particular
file, and will be the same on Windows, Unix, and any other platform
where fossil is run.

Beware, however, that all glob matching is case sensitive. This will
not be a surprise on Unix where all file names are also case
sensitive. However, most Windows file systems are case preserving and
case insensitive. On Windows, the names `ReadMe` and `README` are
names of the same file; on Unix they are different files.

Some example cases:
 
 *  The glob `README` matches only a file named `README` in the root of
    the tree. It does not match a file named `src/README` because it
    does not include any characters that consumed the `src/` part. 
 *  The glob `*/README` does match `src/README`. Unlike Unix file
    globs, it also matches `src/library/README`. However it does not
    match the file `README` in the root of the tree.
 *  The glob `src/README` does match the file named `src\README` on
    Windows because all directory separators are rewritten as `/` in
    the canonical name before the glob is matched. This makes it much
    easier to write globs that work on both Unix and Windows.
 *  The glob `*.[ch]` matches every C source or header file in the
    tree at the root or at any depth. Again, this is (deliberately)
    different from Unix file globs and Windows wild cards.



## Where Globs are Used

### Settings that are Globs

These settings are all lists of glob patterns:

 * `binary-glob`
 * `clean-glob`
 * `crlf-glob`
 * `crnl-glob`
 * `encoding-glob`
 * `ignore-glob`
 * `keep-glob`

All may be [versioned, local, or global][settings]. Use `fossil
settings` to manage local and global settings, or a file in the
repository's `.fossil-settings/` folder at the root of the tree named
for each for versioned setting.

  [settings]: /doc/trunk/www/settings.wiki

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

 * [`add`][]
 * [`addremove`][]
 * [`changes`][]
 * [`clean`][]
 * [`extras`][]
 * [`merge`][]
 * [`settings`][] 
 * [`status`][]
 * [`unset`][]

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


## Platform quirks

The versioned settings files have no platform-specific quirks. Any
GLOBs that matter to your workflow belong there where they can be
safely edited.

Similarly, settings made through the Web UI are platform independent.

GLOBs at the command prompt, however, may need to be protected from
the quirks of the particular shell program you use to type the
command.

The GLOB language is based on common features of Unix (and Linux)
shells. In some cases, this will cause confusion if the shell expands
the GLOB in a way that is similar to what fossil would have done.

When in doubt, the `fossil test-glob` command can be used to see what
fossil saw and what it chose to do. The `fossil test-echo` command is
also handy: it shows exactly what arguments fossil received.

### Windows

Various versions of Windows (a phrase that covers more than just
Window 7 vs Windows 10 because the actual content of `MSVCRT.DLL`, other
DLLs, and even the specific compiler used to build `fossil.exe` can
change the behavior) have subtle differences in how quoting works.

Even without subtle version changes, there are also differences
between the interactive command prompt and `.BAT` or `.CMD` files.

The typical problem is figuring out how to get a GLOB passed on the
command line into `fossil.exe` without it being expanded by either the
shell (CMD never expands globs so that part is trivial) or by the C
runtime startup (which tries hard to expand globs to act like Unix). A
typical example is figuring out how to set `crlf-glob` to `*`.

One approach is
 
    echo * | fossil setting crlf-glob --args -

which works because the built-in command `echo` does not expand its
arguments, and the global option [--args][] reads from `-` which is
replaced by standard input pipe from the `echo` command.

[--args]: https://www.fossil-scm.org/index.html/doc/trunk/www/env-opts.md

Another approach is 

    fossil setting crlf-glob *,

which, despite including the extra comma in the stored setting value,
has the desired effect. The empty GLOB after the comma matches no
files at all, which has no effect since the `*` matches them all.

Similarly, 

    fossil setting crlf-glob '*'

also works. Here the single quotes are unneeded since no white space
is mentioned in the pattern, but do no harm. The GLOB still matches
all the files.


## Implementation and References

Most of the implementation of glob pattern handling in fossil is found
`glob.c`, `file.c`, and each individual command and web page that uses
a glob pattern. Find commands and pages in the fossil sources by
looking for comments like `COMMAND: add` or `WEBPAGE: timeline` in
front of the function that implements the command or page in files
`src/*.c`. (Fossil's build system creates the tables used to dispatch
commands at build time by searching the sources for those comments.) A
few starting points:

 *  [`src/glob.c`][glob.c] implements glob pattern list loading,
    parsing, and matching.
 *  [`src/file.c`][file.c] implements various kinds of canonical
    names of a file.


[glob.c]: https://www.fossil-scm.org/index.html/file/src/glob.c
[file.c]: https://www.fossil-scm.org/index.html/file/src/file.c

The actual pattern matching is implemented in SQL, so the
documentation for `GLOB` and the other string matching operators in
[SQLite] (https://sqlite.org/lang_expr.html#like) is useful. Of
course, the SQLite source code and test harnesses also make
entertaining reading:

 *  `src/func.c` [lines 570-768]
    (https://www.sqlite.org/src/artifact?name=9d52522cc8ae7f5c&ln=570-768) 
 *  `test/expr.test` [lines 586-673]
    (https://www.sqlite.org/src/artifact?name=66a2c9ac34f74f03&ln=586-673) 
