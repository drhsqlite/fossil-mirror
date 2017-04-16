File Name GLOB Patterns
=======================

A number of settings (and options to certain commands as well as query
parameters to certain pages) are documented as one or more GLOB
patterns that will match files either on the disk or in the active
checkout.

A GLOB pattern is described as a pattern that matches file names, and
some of the individual commands show examples of simple GLOBs. The
examples show use of `*` as a wild card, and hint that more is
possible.

In many cases more than one GLOB may be specified as a comma or
white space separated list of GLOB patterns. Several spots in the
command help mention that GLOB patterns may be quoted with single or
double quotes so that spaces and commas may be included in the pattern
if needed.

Outside of this document, only the source code contains the exact
specification of the complete syntax of a GLOB pattern.

## Syntax

    any     Any character not mentioned matches exactly that character
    *       Matches any sequence of zero or more characters.
    ?       Matches exactly one character.
    [...]   Matches one character from the enclosed list of characters.
    [^...]  Matches one character not in the enclosed list.

Lists of characters have some additional features. 

 * A range of characters may be specified with `-`, so `[a-d]` matches
   exactly the same characters as `[abcd]`.
 * Include `-` in a list by placing it last, just before the `]`.
 * Include `]` in a list by making the first character after the `[` or
   `[^`. At any other place, `]` ends the list. 
 * Include `^` in a list by placing anywhere except first after the
   `[`.


Some examples:

    [a-d]   Matches any one of 'a', 'b', 'c', or 'd'
    [a-]    Matches either 'a' or '-'
    [][]    Matches either ']' or '['
    [^]]    Matches exactly one character other than ']'
    []^]    Matches either ']' or '^'

The glob is compared to the canonical name of the file in the checkout
tree, and must match the entire name to be considered a match.

Unlike typical Unix shell globs, wildcard sequences are allowed to
match '/' directory separators as well as the initial '.' in the name
of a hidden file or directory.

A list of GLOBs is simply one or more GLOBs separated by whitespace or
commas. If a GLOB must contain a space or comma, it can be quoted with
either single or double quotation marks.

Since a newline is considered to be whitespace, a list of GLOBs in a
file (as for a versioned setting) may have one GLOB per line.


## File names to match

Before comparing to a GLOB pattern, each file name is transformed to a
canonical form. Although the real process is more complicated, the
canonical name of a file has all directory separators changed to `/`,
and all `/./` and `/../` sequences removed. The goal is a name that is
the simplest possible while still specific to each particular file.

This has some consequences. 

The simplest GLOB pattern is just a bare name of a file named with the
usual assortment of allowed file name characters. Such a pattern
matches that one file: the GLOB `README` matches only a file named
`README` in the root of the tree. The GLOB `*/README` would match a
file named `README` anywhere except the root, since the glob requires
that at least one '/' be in the name. (Recall that `/` matches the
directory separator regardless of whether it is `/` or `\` on your
system.)




## Where are they used

### Settings that use GLOBs

These settings are all lists of GLOBs. All may be global, local, or
versioned. Use `fossil settings` to manage global and local settings,
or file in the repository's `.fossil-settings/` folder named for each
for versioned setting.

 * `binary-glob`
 * `clean-glob`
 * `crlf-glob`
 * `crnl-glob`
 * `encoding-glob`
 * `ignore-glob`
 * `keep-glob`


### Commands that refer to GLOBs

Many of the commands that respect the settings containing GLOBs have
options to override some or all of the settings.

 * `add`
 * `addremove`
 * `changes`
 * `clean`
 * `extras`
 * `merge`
 * `settings` 
 * `status`
 * `unset`

The commands `tarball` and `zip` produce compressed archives of a specific
checkin. They may be further restricted by options that specify GLOBs
that name files to include or exclude rather than taking the entire
checkin.

The commands `http`, `cgi`, `server`, and `ui` that implement or support with web servers
provide a mechanism to name some files to serve with static content
where a list of GLOBs specifies what content may be served.


### Web pages that refer to GLOBs

The /timeline page supports a query parameter that names a GLOB of
files to focus the timeline on. It also can use `GLOB`, `LIKE`, or
`REGEXP` matching on tag names, where each is implemented by the
corresponding operator in [SQLite][].

The pages `/tarball` and `/zip` generate compressed archives of a
specific checkin. They may be further restricted by query parameters
that specify GLOBs that name files to include or exclude rather than
taking the entire checkin.


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


## Implementation

Most of the implementation of GLOB handling is found in
[`src/glob.c`][glob.c].

The actual matching is implemented in SQL, so the documentation for
`GLOB` and the other string matching operators in [SQLite][] is
useful. 

[glob.c]: https://www.fossil-scm.org/index.html/file/src/glob.c
[SQLite]: https://sqlite.org/lang_expr.html#like

