# Colorized Diffs

The oldest and most widely compatible method to get colorized diffs in
Fossil is to use its web UI:

    fossil ui --page '/vdiff?from=2024-04-01&to=trunk'

That syntax is admittedly awkward, and it doesn’t work where “from” is
the current checkout.  Fortunately, there are many other methods to get
colorized `diff` output from Fossil.


<a id="ui"></a>
## `fossil diff -b`

This produces a graphical diff in HTML format and sends it to the
user’s default web browser for viewing.



<a id="ui"></a>
## `fossil diff -tk`

You may be surprised to learn that the prior feature doesn’t use any of
the skinning or chrome from Fossil UI. This is because it is meant as a
functional replacement for an older method of getting colorized diffs,
“`fossil diff -tk`”. The feature was added after Apple stopped shipping
Tcl/Tk in macOS, and the third-party replacements often failed to work
correctly. It’s useful on other platforms as well.


<a id="git"></a>
## Delegate to Git

It may be considered sacrilege by some, but the most direct method for
those who want Git-like diff behavior may be to delegate diff behavior
to Git:

    fossil set --global diff-command 'git diff --no-index'

The flag permits it to diff files that aren’t inside a Git repository.


<a id="diffutils"></a>
## GNU Diffutils

If your system is from 2016 or later, it may include [GNU Diffutils][gd]
3.4 or newer, which lets you say:

    fossil set --global diff-command 'diff -dwu --color=always'

You might think you could give `--color=auto`, but that fails with
commands like “`fossil diff | less`” since the pipe turns the output
non-interactive from the perspective of the underlying `diff` instance.

This use of unconditional colorization means you will then have to
remember to add the `-i` option to `fossil diff` commands when producing
`patch(1)` files or piping diff output to another command that doesn’t
understand ANSI escape sequences, such as [`diffstat`][ds].

[ds]: https://invisible-island.net/diffstat/
[gd]: https://www.gnu.org/software/diffutils/


<a id="bat"></a>
## Bat, the Cat with Wings

We can work around the `--color=auto` problem by switching from GNU less
as our pager to [`bat`][bat], as it can detect GNU diff output and
colorize it for you:

    fossil set --global diff-command 'diff -dwu --color=auto'
    fossil diff | bat

In this author’s experience, that works a lot more reliably than GNU
less’s ANSI color escape code handling, even when you set `LESS=-R` in
your environment.

The reason we don’t leave the `diff-command` unset in this case is that
Fossil produces additional lines at the start which confuse the diff
format detection in `bat`. Forcing output through an external diff
command solves that. It also means that if you forget to pipe the output
through `bat`, you still get colorized output from GNU diff.

[bat]: https://github.com/sharkdp/bat


<a id="colordiff"></a>
## Colordiff

A method that works on systems predating GNU diffutils 3.4 or the
widespread availability of `bat` is to install [`colordiff`][cdurl], as
it is included in [many package systems][cdpkg], including ones for
outdated OSes. That then lets you say:

    fossil set --global diff-command 'colordiff -dwu'

The main reason we list this alternative last is that it has the same
limitation of unconditional color as [above](#diffutils).

[cdurl]: https://www.colordiff.org/
[cdpkg]: https://repology.org/project/colordiff/versions

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
