# Check-Out Workflows

Because Fossil separates the concept of “check-out directory” from
“repository DB file,” it gives you the freedom to choose from several
working styles. Contrast Git, where the two concepts are normally
intermingled in a single working directory, which strongly encourages
the “update in place” working style.


## <a id="mcw"></a> Multiple-Checkout Workflow

With Fossil, it is routine to have multiple check-outs from the same
repository:

    fossil clone https://example.com/repo /path/to/repo.fossil

    mkdir -p ~/src/my-project/trunk
    cd ~/src/my-project/trunk
    fossil open /path/to/repo.fossil    # implicitly opens “trunk”

    mkdir ../release
    cd ../release
    fossil open /path/to/repo.fossil release

    mkdir ../my-other-branch
    cd ../my-other-branch
    fossil open /path/to/repo.fossil my-other-branch

    mkdir ../scratch
    cd ../scratch
    fossil open /path/to/repo.fossil abcd1234

    mkdir ../test
    cd ../test
    fossil open /path/to/repo.fossil 2019-04-01

Now you have five separate check-out directories: one each for:

*   trunk
*   the latest tagged public release
*   an alternate branch you’re working on
*   a “scratch” directory for experiments you don’t want to do in the
    other check-out directories; and
*   a “test” directory where you’re currently running a long-running
    test to evaluate a user bug report against the version as of last
    April Fool’s Day.

Each check-out operates independently of the others.

This multiple-checkouts working style is especially useful when Fossil stores source code in programming languages
where there is a “build” step that transforms source files into files
you actually run or distribute. Contrast a switch-in-place workflow,
where you have to rebuild all outputs from the source files
that differ between those versions whenever you switch versions. In the above model,
you switch versions with a “`cd`” command instead, so that you only have
to rebuild outputs from files you yourself change.

This style is also useful when a check-out directory may be tied up with
some long-running process, as with the “test” example above, where you
might need to run an hours-long brute-force replication script to tickle
a [Heisenbug][hb], forcing it to show itself. While that runs, you can
open a new terminal tab, “`cd ../trunk`”, and get back
to work.

[hb]:     https://en.wikipedia.org/wiki/Heisenbug



## <a id="scw"></a> Single-Checkout Workflows

Nevertheless, it is possible to work in a more typical Git sort of
style, switching between versions in a single check-out directory.

#### <a id="idiomatic"></a> The Idiomatic Fossil Way

The most idiomatic way is as follows:

    fossil clone https://example.com/repo /path/to/repo.fossil
    mkdir work-dir
    cd work-dir
    fossil open /path/to/repo.fossil
    ...work on trunk...

    fossil update my-other-branch
    ...work on your other branch in the same directory...

Basically, you replace the `cd` commands in the multiple checkouts
workflow above with `fossil up` commands.


#### <a id="open"></a> Opening a Repository by URI

You can instead open the repo’s URI directly:

    mkdir work-dir
    cd work-dir
    fossil open https://example.com/repo

Now you have “trunk” open in `work-dir`, with the repo file stored as
`repo.fossil` in that same directory.

Users of Git may be surprised that it doesn’t create a directory for you
and that you `cd` into it *before* the clone-and-open step, not after.
This is because we’re overloading the “open” command, which already had
the behavior of opening into the current working directory. Changing it
to behave like `git clone` would therefore make the behavior surprising
to Fossil users. (See [our discussions][caod] if you want the full
details.)


#### <a id="clone"></a> Git-Like Clone-and-Open

Fossil also supports a more Git-like alternative:

    fossil clone https://fossil-scm.org/fossil
    cd fossil

This results in a `fossil.fossil` repo DB file and a `fossil/` working
directory.

Note that our `clone URI` behavior does not commingle the repo and
check-out, solving our major problem with the Git design.

If you want the repo to be named something else, adjust the URL:

    fossil clone https://fossil-scm.org/fossil/fsl

That gets you `fsl.fossil` checked out into `fsl/`.

For sites where the repo isn’t served from a subdirectory like this, you
might need another form of the URL. For example, you might have your
repo served from `dev.example.com` and want it cloned as `my-project`:

    fossil clone https://dev.example.com/repo/my-project

The `/repo` addition is the key: whatever comes after is used as the
repository name. [See the docs][clone] for more details.

[caod]:  https://fossil-scm.org/forum/forumpost/3f143cec74
[clone]: /help?cmd=clone

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
