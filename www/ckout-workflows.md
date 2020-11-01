# Check-Out Workflows

Because Fossil separates the concept of “check-out directory” from
“repository DB file,” it gives you the freedom to choose from several
working styles. Contrast Git, where the two concepts are normally
intermingled in a single working directory, which strongly encourages
the “update in place” working style, leaving its `git-worktree` feature
underutilized.


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
you actually run or distribute. With Git’s typical switch-in-place workflow,
you have to rebuild all outputs from the source files
that differ between those versions whenever you switch versions. In the above Fossil working model,
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

With the clone done as in [the prior section](#mdw), the most idiomatic
way is as follows:

        mkdir work-dir
        cd work-dir
        fossil open /path/to/repo.fossil
        ...work on trunk...

        fossil update my-other-branch
        ...work on your other branch in the same directory...

Basically, you replace the `cd` commands in the multiple checkouts
workflow above with `fossil up` commands.


#### <a id="open"></a> The Clone-and-Open Way

In Fossil 2.12, we added a feature that allows you to get closer to
Git’s single-step clone-and-open behavior:

        mkdir work-dir
        cd work-dir
        fossil open https://example.com/repo

Now you have “trunk” open in `work-dir`, with the repo file stored as
`repo.fossil` in that same directory.

The use of [`fossil open`][open] here instead of [`fossil clone`][clone]
is likely to surprise a Git user. When we were [discussing][caod]
this, we considered following the Git command style, but we decided
against it because it goes against this core Fossil design principle:
given that the Fossil repo is separate from the check-out, why would you
expect asking for a repo clone to also create a check-out directory for
you?  We view commingled repository + check-out as a design error in
Git, so why would we repeat the error?

To see why we see this behavior is error-prone, consider that
`git clean` must have an exception to avoid nuking the `.git` directory.
We had to add that complication to `fossil clean` when we added the
`fossil open URI` feature: it won’t nuke the repo DB file.

[clone]:  /help?cmd=clone
[open]:   /help?cmd=open


#### <a id="clone"></a> The Git Clone Way

This feature didn’t placate many Git fans, though, so with Fossil 2.14 —
currently unreleased — we now allow this:

        fossil clone https://fossil-scm.org/fossil

This results in a `fossil.fossil` repo DB file and a `fossil/` working
directory.

Note that our `clone URI` behavior does not commingle the repo and
check-out, solving our major problem with the Git design, though we
still believe it to be confusing to have “clone” be part of “open,” and
still more confusing to have “open” part of “clone.” We prefer keeping
these operations entirely separate, either as at the [top of this
section](#scw) or [as in the prior one](#mcw). Still, please yourself.

If you want the repo to be named something else, adjust the URL:

        fossil clone https://fossil-scm.org/fossil/fsl

That gets you `fsl.fossil` checked out into `fsl/`.

For sites where the repo isn’t served from a subdirectory like this, you
might need another form of the URL. For example, you might have your
repo served from `dev.example.com` and want it cloned as `my-project`:

        fossil clone https://dev.example.com/repo/my-project

The `/repo` addition is the key: whatever comes after is used as the
repository name. [See the docs][clone] for more details.

[caod]: https://fossil-scm.org/forum/forumpost/3f143cec74

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
