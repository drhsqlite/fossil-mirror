# Git to Fossil Translation Guide

## Introduction

Fossil shares many similarities with Git.  In many cases, the
sub-commands are identical: [`fossil bisect`][fbis] does essentially the
same thing as [`git bisect`][gbis], for example.

Yet, Fossil is not merely Git with a bunch of commands misspelled. If
that were the case, we could give you a two-column translation table
which would tell you [how to say things like “`git reset --hard HEAD`”](#reset) in
this funny ol’ Fossil dialect of Git and be done. The purpose of this
document is to cover all the cases where there is no simple 1:1 mapping,
usually because of intentional design differences in Fossil that prevent
it from working exactly like Git. We choose to explain these differences
since to understand the conversion, you need to know why each difference
exists.

We focus on practical command examples here, leaving discussions of the
philosophical underpinnings that drive these command differences to [another
document][fvg]. The [case studies](#cs1) do get a bit philosophical, but
it is with the aim of illustrating how these Fossil design differences
cause Fossil to behave materially differently from Git in everyday
operation.

We present this from the perspective of Git users moving to Fossil, but
it is also possible to read this document as a Fossil user who speaks
only pidgin Git, who may often have questions of the form, “Now how do I
do X in Git again?”

This document’s authors are intimately familiar with Fossil, so it is
difficult for us to anticipate the perspective of people who are
intimately familiar with Git. If you have a lot of prior Git
experience, we welcome your contributions and questions on the [Fossil
Forum][ffor].

While we do try to explain Fossil-specific terminology inline here
as-needed, you may find it helpful to skim [the Fossil glossary][gloss].
It will give you another take on our definitions here, and it may help
you to understand some of the other Fossil docs better.

[fbis]:  /help?cmd=bisect
[gbis]:  https://git-scm.com/docs/git-bisect
[ffor]:  https://fossil-scm.org/forum
[fvg]:   ./fossil-v-git.wiki


<a id="mwd"></a>
## Repositories and Checkouts Are Distinct

A repository and a check-out are distinct in Fossil, allowing them to be
stored in separate directory trees, whereas the two are commingled by
default with Git, with the repository stored in a `.git` subdirectory
underneath your working directory. This difference shows up in several
separate places when it comes to moving from Git to Fossil.



#### <a id="cwork" name="scw"></a> Checkout Workflows

A Fossil repository is an SQLite database storing the entire history of a
project. It is not normally stored inside the working tree.
A Fossil working tree — [also called a check-out](./glossary.md#check-out) — is a directory
that contains a snapshot of your project that you are currently working
on, extracted for you from the repository database file by the `fossil`
program.

There are ways to
[emulate the Fossil working style in Git](#worktree), but because they’re not
designed into the core concept of the tool, Git tutorials usually
advocate a switch-in-place working mode instead, so that is how most
users end up working with Git. Contrast [Fossil’s check-out workflow
document][ckwf] to see the practical differences.

There is one Git-specific detail we wish to add beyond what that
document already covers. This command:

    git checkout some-branch

…is best given as:

    fossil update some-branch

…in Fossil. There is a [`fossil checkout`][co] command, but it has
[several differences](./co-vs-up.md) that make it less broadly useful
than [`fossil update`][up] in everyday operation, so we recommend that
Git users moving to Fossil develop a habit of typing `fossil up` rather
than `fossil checkout`. That said, one of those differences does match
up with Git users’ expectations: `fossil checkout` doesn’t pull changes
from the remote repository into the local clone as `fossil update` does.
We think this is less broadly useful, but that’s the subject of the next
section.

[ckwf]: ./ckout-workflows.md
[co]:   /help?cmd=checkout


#### <a id="pullup"></a> Update vs Pull

The closest equivalent to [`git pull`][gpull] is not
[`fossil pull`][fpull], but in fact [`fossil up`][up].

This is because
Fossil tends to follow the CVS command design: `cvs up` pulls
changes from the central CVS repository and merges them into the local
working directory, so that’s what `fossil up` does, too. (This design
choice also tends to make Fossil feel comfortable to Subversion
expatriates.)

The `fossil pull` command is simply the reverse of
`fossil push`, so that `fossil sync` [is functionally equivalent
to](./sync.wiki#sync):

    fossil push ; fossil pull

There is no implicit “and update the local working directory” step in Fossil’s
push, pull, or sync commands, as there is with `git pull`.

Someone coming from the Git perspective may perceive that `fossil up`
has two purposes:

*   Without the optional `VERSION` argument, it updates the working
    check-out to the tip of the current branch, as `git pull` does.

*   Given a `VERSION` argument, it updates to the named version. If that’s the
    name of a branch, it updates to the *tip* of that branch, as
    `git checkout BRANCH` does.

In fact, these are the same operation, so they’re the same command in
Fossil. The first form simply allows the `VERSION` to be implicit: the
tip of the current branch.

We think this is a more sensible command design than `git pull` vs
`git checkout`. ([…vs `git checkout` vs `git checkout`!][gcokoan])

[fpull]:   /help?cmd=pull
[gpull]:   https://git-scm.com/docs/git-pull
[gcokoan]: https://stevelosh.com/blog/2013/04/git-koans/#s2-one-thing-well


#### <a id="close" name="dotfile"></a> Closing a Check-Out

The [`fossil close`][close] command dissociates a check-out directory from the
Fossil repository database, _nondestructively_ inverting [`fossil open`][open].
(Contrast Git’s [closest alternative](#worktree), `git worktree remove`, which *is*
destructive!) This Fossil command does not
remove the managed files, and unless you give the `--force`
option, it won’t let you close the check-out with uncommitted changes to
those managed files.

The `close` command also refuses to run without `--force` when you have
certain other precious per-checkout data that Fossil stores in the
`.fslckout` file at the root of a check-out directory. This is an SQLite
database that keeps track of local state such as what version you have
checked out, the contents of the [stash] for that working directory, the
[undo] buffers, per-checkout [settings][set], and so forth. The stash
and undo buffers are considered precious uncommitted changes,
so you have to force Fossil to discard these as part of closing the
check-out.

Thus, `.fslckout` is not the same thing as `.git`!

In native Windows builds of Fossil — that is, excluding Cygwin and WSL
builds, which follow POSIX conventions —  this file is called `_FOSSIL_`
instead to get around the historical 3-character extension limit with
certain legacy filesystems.

Closing a check-out directory is a rare operation. One use case
is that you’re about to delete the directory, so you want Fossil to forget about it
for the purposes of commands like [`fossil all`][all]. Even that isn’t
necessary, because Fossil will detect that this has happened and forget
the working directory for you.

[all]: /help?cmd=all


#### <a id="worktree"></a> Git Worktrees

There are at least three different ways to get [Fossil-style multiple
check-out directories][mcw] with Git.

The old way is to simply symlink the `.git` directory between working
trees:

    mkdir ../foo-branch
    ln -s ../actual-clone-dir/.git .
    git checkout foo-branch

The symlink trick has a number of problems, the largest being that
symlinks weren’t available on Windows until Vista, and until the Windows
10 Creators Update was released in spring of 2017, you had to be an
Administrator to use the feature besides. ([Source][wsyml]) Git 2.5 solved
this problem back when Windows XP was Microsoft’s current offering
by adding the `git-worktree` command:

    git worktree add ../foo-branch foo-branch
    cd ../foo-branch

That is approximately equivalent to this in Fossil:

    mkdir ../foo-branch
    cd ../foo-branch
    fossil open /path/to/repo.fossil foo-branch

The Fossil alternative is wordier, but since this tends to be one-time setup,
not something you do everyday, the overhead is insignificant. This author keeps a “scratch” check-out
for cases where it’s inappropriate to reuse the “trunk” check-out,
isolating all of my expedient switch-in-place actions to that one
working directory. Since the other peer check-outs track long-lived
branches, and that set rarely changes once a development machine is set
up, I rarely pay the cost of these wordier commands.

That then leads us to the closest equivalent in Git to [closing a Fossil
check-out](#close):

    git worktree remove .

Note, however, that unlike `fossil close`, once the Git command
determines that there are no uncommitted changes, it blows away all of
the checked-out files! Fossil’s alternative is shorter, easier to
remember, and safer.

There’s another way to get Fossil-like separate worktrees in Git:

    git clone --separate-git-dir repo.git https://example.com/repo

This allows you to have your Git repository directory entirely separate
from your working tree, with `.git` in the check-out directory being a
file that points to `../repo.git`, in this example.

[mcw]:   ./ckout-workflows.md#mcw
[wsyml]: https://blogs.windows.com/windowsdeveloper/2016/12/02/symlinks-windows-10/


#### <a id="iip"></a> Init in Place

To illustrate the differences that Fossil’s separation of repository
from working directory creates in practice, consider this common Git “init in place”
method for creating a new repository from an existing tree of files,
perhaps because you are placing that project under version control for
the first time:

    cd long-established-project
    git init
    git add *
    git commit -m "Initial commit of project."

The closest equivalent in Fossil is:

    cd long-established-project
    fossil init .fsl
    fossil open --force .fsl
    fossil add *
    fossil ci -m "Initial commit of project."

Note that unlike in Git, you can abbreviate the “`commit`” command in
Fossil as “`ci`” for compatibility with CVS, Subversion, etc.

This creates a `.fsl` repo DB at the root of the project check-out to
emulate the `.git` repo dir. We have to use the `--force` flag on
opening the new repo because Fossil expects you to open a repo into an
empty directory in order to avoid spamming the contents of a repo over
an existing directory full of files. Here, we know the directory
contains files that will soon belong in the repository, though, so we
override this check. From then on, Fossil works like Git, for the
purposes of this example.

We’ve drawn this example to create a tight parallel between Fossil and
Git, not to commend this `.fsl`-at-project-root trick to you. A better
choice would be `~/museum/home/long-established-project.fossil`, if
you’re following [the directory scheme exemplified in the glossary](./glossary.md#repository). That said, it
does emphasize an earlier point: Fossil doesn’t care where you put the
repo DB file or what you name it.


[clone]:  /help?cmd=clone
[close]:  /help?cmd=close
[gloss]:  ./glossary.md
[open]:   /help?cmd=open
[set]:    /help?cmd=setting
[server]: /help?cmd=server
[stash]:  /help?cmd=stash
[undo]:   /help?cmd=undo


## <a id="log"></a> Fossil’s Timeline Is the “Log”

Git users often need to use the `git log` command to dig linearly through
commit histories due to its [weak data model][wdm], giving [O(n)
performance][ocomp].

Fossil parses a huge amount of information out of commits that allow it
to produce its [timeline CLI][tlc] and [its `/timeline` web view][tlw]
using indexed SQL lookups, which generally have the info you would have
to manually extract from `git log`, produced much more quickly than Git
can, all else being equal: operations over [SQLite’s B-tree data structures][btree]
generally run in O(log n) time, faster than O(n) for equal *n* when the
constants are equal.

Yet the constants are *not* equal because Fossil
reads from a single disk file rather than visit potentially many
files in sequence as Git must, so the OS’s buffer cache can result in
[still better performance][35pct].

Unlike Git’s log, Fossil’s timeline shows info across all branches by
default, a feature for maintaining better situational awareness.
It is possible to restrict the timeline to a single branch using `fossil timeline -b`.
Similarly, to restrict the timeline using the web UI equivalent,
click the name of a branch on the `/timeline` or `/brlist` page. (Or
manually, by adding the `r=` query parameter.) Note that even in this
case, the Fossil timeline still shows other branches where they interact
with the one you’ve referenced in this way; again, better situational
awareness.


#### <a id="emu-log"></a> Emulating `git log`

If you truly need a backwards-in-time-only view of history in Fossil to
emulate `git log`, this is as close as you can currently come:

    fossil timeline parents current

Again, though, this isn’t restricted to a single branch, as `git log`
is.

Another useful rough equivalent is:

    git log --raw
    fossil time -v

This shows what changed in each version, though Fossil’s view is more a
summary than a list of raw changes. To dig deeper into single commits,
you can use Fossil’s [`info` command][infoc] or its [`/info` view][infow].

Inversely, you may more exactly emulate the default `fossil timeline`
output with `git log --name-status`.


#### <a id="whatchanged"></a> What Changed?

A related — though deprecated — command is `git whatchanged`, which gives results similar to
`git log --raw`, so we cover it here.

Though there is no `fossil whatchanged` command, the same sort of
information is available. For example, to pull the current changes from
the remote repository and then inspect them before updating the local
working directory, you might say this in Git:

    git fetch
    git whatchanged ..@{u}

…which you can approximate in Fossil as:

    fossil pull
    fossil up -n
    fossil diff --from tip

To invert the `diff` to show a more natural patch, the command needs to
be a bit more complicated, since you can’t currently give `--to`
without `--from`.

    fossil diff --from current --to tip

Rather than use the “dry run” form of [the `update` command][up], you can
say:

    fossil timeline after current

…or if you want to restrict the output to the current branch:

    fossil timeline descendants current


#### <a id="ckin-names"></a> Symbolic Check-In Names

Note the use of [human-readable symbolic version names][scin] in Fossil
rather than [Git’s cryptic notations][gcn].

For a more dramatic example of this, let us ask Git, “What changed since the
beginning of last month?” being October 2020 as I write this:

    git log master@{2020-10-01}..HEAD

That’s rather obscure! Fossil answers the same question with a simpler
command:

    fossil timeline after 2020-10-01

You may need to add `-n 0` to bypass the default output limit of
`fossil timeline`, 20 entries. Without that, this command reads
almost like English.

Some Git users like to write commands like the above so:

    git log @{2020-10-01}..@

Is that better? “@” now means two different things: an at-time reference
and a shortcut for `HEAD`!

If you are one of those that like short commands, Fossil’s method is
less cryptic: it lets you shorten words in most cases up to the point
that they become ambiguous. For example, you may abbreviate the last
`fossil` command in the prior section:

    fossil tim d c

…beyond which the `timeline` command becomes ambiguous with `ticket`.

Some Fossil users employ shell aliases, symlinks, or scripts to shorten
the command still further:

    alias f=fossil
    f tim d c

Granted, that’s rather obscure, but you you can also choose something
intermediate like “`f time desc curr`”, which is reasonably clear.

[35pct]: https://www.sqlite.org/fasterthanfs.html
[btree]: https://sqlite.org/btreemodule.html
[gcn]:   https://git-scm.com/docs/gitrevisions
[infoc]: /help?cmd=info
[infow]: /help?cmd=/info
[ocomp]: https://www.bigocheatsheet.com/
[tlc]:   /help?cmd=timeline
[tlw]:   /help?cmd=/timeline
[up]:    /help?cmd=update
[wdm]:   ./fossil-v-git.wiki#durable


## <a id="dhead"></a> Detached HEAD State

The SQL indexes in Fossil which we brought up above have a useful
side benefit: you cannot have a [detached HEAD state][gdh] in Fossil,
the source of untold pain and data loss in Git. It simply cannot be done
in Fossil, because the indexes always let us find our way back into the
hash tree.


## <a id="slcom"></a> Summary Line Convention in Commit Comments

The Git convention of a [length-limited summary line][lsl] at the start
of commit comments is not enforced or obeyed by default in Fossil.
However, there is a setting under Admin → Timeline → “Truncate comment
at first blank line (Git-style)” to change this for `/timeline`
displays.  Alternately, you could enable the “Allow block-markup in
timeline” setting under Admin → Timeline, then apply [local skin
customizations][cskin] to put that first comment in bold or whatever
suits.

Because this isn’t a typical Fossil convention, you’re likely to find
other odd differences between it and Git-based infrastructure.  For
instance, Vim doesn’t ship with syntax support for Fossil commit
messages if you set `EDITOR=vim` in your shell environment, so you won’t
get over-limit highlighting for first-line text beyond the 50th
character and such, because it doesn’t recognize Fossil commit messages
and apply similar rules as to Git commit messages.

[cskin]: ./customskin.md
[lsl]:   https://chris.beams.io/posts/git-commit/#limit-50


<a id="autocommit"></a>
## Fossil Never Auto-Commits

There are several features in Git besides its `commit` command that
produce a new commit to the repository, and by default, they do it
without prompting or even asking for a commit message. These include
Git’s [`rebase`](#rebase), `merge`, and [`cherrypick`](#cpickrev)
commands, plus the [commit splitting](#comsplit) sub-feature
“`git commit -p`”.

Fossil never does this, on firm philosophical grounds: we wish to be
able to test that each potentially repository-changing command does not
break anything _before_ freezing it immutably into the [Merkle
tree](./blockchain.md). Where Fossil has equivalent commands, they
modify the checkout tree alone, requiring a separate `commit` command
afterward, withheld until the user has satisfied themselves that the
command’s result is correct.

We believe this is the main reason Git lacks an [autosync](#autosync)
feature: making push a manual step gives the user a chance to rewrite
history after triggering one of these autocommits locally, should the
automatic commit fail to work out as expected.  Fossil chooses the
inverse path under the philosophy that commits are *commitments,* not
something you’re allowed to go back and rewrite later.

This is also why there is no automatic commit message writing feature in
Fossil, as in these autocommit-triggering Git commands. The user is
meant to write the commit message by hand after they are sure it’s
correct, in clear-headed retrospective fashion.  Having the tool do it
prospectively before one can test the result is simply backwards.


<a id="staging"></a>
## There Is No Staging Area

Fossil omits the "Git index" or "staging area" concept.  When you
type "`fossil commit`" _all_ changes in your check-out are committed,
by default.  There is no need for the "-a" option as with Git.

If you only want to commit _some_ of the changes, list the names
of the files or directories you want to commit as arguments, like this:

    fossil commit src/feature.c doc/feature.md examples/feature

Note that the last element is a directory name, meaning “any changed
file under the `examples/feature` directory.”


<a id="comsplit"></a>
## Commit Splitting

[Git’s commit splitting features][gcspl] rely on
other features of Git that Fossil purposefully lacks, as covered in the
prior two sections: [autocommit](#autocommit) and [the staging
area](#staging).

While there is no direct Fossil equivalent for
`git add -p`, `git commit -p`, or `git rebase -i`, you can get the same
effect by converting an uncommitted change set to a patch and then
running it through [Patchouli].

Rather than use `fossil diff -i` to produce such a patch, a safer and
more idiomatic method would be:

    fossil stash save -m 'my big ball-o-hackage'
    fossil stash diff > my-changes.patch

That stores your changes in the stash, then lets you operate on a copy
of that patch. Each time you re-run the second command, it will take the
current state of the working directory into account to produce a
potentially different patch, likely smaller because it leaves out patch
hunks already applied.

In this way, the combination of working tree and stash replaces the need
for Git’s index feature.

We believe we know how to do commit splitting in a way compatible with
the Fossil philosophy, without following Git’s ill-considered design
leads. It amounts to automating the above process through an interactive
variant of [`fossil stash apply`][stash], as currently prototyped in the
third-party tool [`fnc`][fnc] and [its interactive `stash`
command][fncsta]. We merely await someone’s [contribution][ctrb] of this
feature into Fossil proper.

[ctrb]:      https://fossil-scm.org/fossil/doc/trunk/www/contribute.wiki
[fnc]:       https://fnc.bsdbox.org/
[fncsta]:    https://fnc.bsdbox.org/uv/doc/fnc.1.html#stash
[gcspl]:     https://git-scm.com/docs/git-rebase#_splitting_commits
[Patchouli]: https://pypi.org/project/patchouli/


<a id="bneed"></a>
## Create Branches at Point of Need, Rather Than Ahead of Need

Fossil prefers that you create new branches as part of the first commit
on that branch:

    fossil commit --branch my-branch

If that commit is successful, your local check-out directory is then
switched to the tip of that branch, so subsequent commits don’t need the
“`--branch`” option. You simply say `fossil commit` again to continue
adding commits to the tip of that branch.

To switch back to the parent branch, say something like:

    fossil update trunk

(This is approximately equivalent to `git checkout master`.)

Fossil does also support the Git style, creating the branch ahead of
need:

    fossil branch new my-branch
    fossil up my-branch
    ...work on first commit...
    fossil commit

This is more verbose, giving the same overall effect though the initial
actions are inverted: create a new branch for the first commit, switch
the check-out directory to that branch, and make that first commit. As
above, subsequent commits are descendants of that initial branch commit.
We think you’ll agree that creating a branch as part of the initial
commit is simpler.

Fossil also allows you to move a check-in to a different branch
*after* you commit it, using the "`fossil amend`" command.
For example:

    fossil amend current --branch my-branch

This works by inserting a tag into the repository that causes the web UI
to relabel commits from that point forward with the new name. Like Git,
Fossil’s fundamental data structure is the interlinked DAG of commit
hashes; branch names are supplemental data for making it easier for the
humans to understand this DAG, so this command does not change the core
history of the project, only annotate it for better display to the
humans.

(The version string “current” is one of the [special check-in names][scin] in Fossil. See
that document for the many other names you can give to “`amend`”, or
indeed to any other Fossil command documented to accept a `VERSION` or
`NAME` string.)

[scin]: ./checkin_names.wiki


<a id="syncall"></a>
## Sync Is All-or-Nothing

Fossil does not support the concept of syncing, pushing, or pulling
individual branches.  When you sync/push/pull in Fossil, it
processes all artifacts in its hash tree:
branches, tags, wiki articles, tickets, forum posts, technotes…
This is [not quite “everything,” full stop][bu], but it’s close.
[Fossil is an AP-mode system][capt], which in this case means it works
*very hard* to ensure that all repos are as close to identical as it can
make them under this eventually-consistent design philosophy.

Branch *names* sync automatically in Fossil, not just the
content of those branches. That means this common Git command:

    git push origin master

…is simply this in Fossil:

    fossil push

Fossil doesn’t need to be told what to push or where to push it: it just
keeps using the same remote server URL you gave it last
until you [tell it to do something different][rem]. It pushes all
branches, not just one named local branch.

[capt]: ./cap-theorem.md
[rem]:  /help?cmd=remote


<a id="autosync"></a>
## Autosync

Fossil’s [autosync][wflow] feature, normally enabled, has no
equivalent in Git. If you want Fossil to behave like Git, you can turn
it off:

    fossil set autosync 0

Let’s say that you have a typical server-and-workstations model with two
working clones on different machines, that you have disabled autosync,
and that this common sequence then occurs:

1.  Alice commits to her local clone and *separately* pushes the change
    up to Condor — their central server — in typical Git fashion.
2.  Bob does the same.
3.  Alice brings Bob’s changes down from Condor with “`fossil pull`,” sees
    what he did to their shared working branch, and becomes most wrathful.
    (Tsk, tsk.)

We’ll get to what you do about this situation [below](#reset), but for
now let us focus on the fact that disabling autosync makes it easier for
[forks] to occur in the development history. If all three machines had
been online and syncing at the time the sequence above began, Bob would
have been warned in step 2 that committing to the central repo would
create a fork and would be invited to fix it before committing.
Likewise, it would allow Fossil to warn Alice about the new
tip-of-branch commit the next time she triggers an implicit autosync at
step 3, giving her a chance to bring Bob’s changes down in a
non-conflicting manner, allowing work to proceed with minimal fuss.

Fossil, being a distributed version control system, cannot guarantee
that sequence of events. Because it allows Alice’s work to proceed
asynchronously, it gives her the chance to create *another* inadvertent
fork before she can trigger an autosync. This is not a serious problem;
Fossil resolves it the same way as with Bob, by inviting her to fix this
second fork in the same manner as it did with Bob. It gets both parties
back onto a single track as expeditiously as possible by moving the
synchronization point out of the expensive human-time workflow and into
the software system, where it’s cheapest to resolve.

Autosync provides Fossil with most of the advantages of a centralized
version control system while retaining the advantages of distributed
version control:

1.  Your work stays synced up with your coworkers’ efforts as long as your
    machine can connect to the remote repository. At need, you can go
    off-network and continue work atop the last version you synced with
    the remote.

2.  You get implicit off-machine backup of your commits. Unlike
    centralized version control, though, you can still work while
    disconnected; your changes will sync up with the remote once you get
    back online.

3.  Because there is [little distinction][bu] between the clones in the Fossil
    model — unlike in Git, where clones often quickly diverge from each
    other, quite possibly on purpose — the backup advantage applies in inverse
    as well: if the remote server falls over dead, one of those with a
    clone of that repository can stand it back up, and everyone can get
    back to work simply by re-pointing their local repo at the new
    remote.  If the failed remote comes back later, it can sync with the
    new central version, then perhaps take over as the primary source of
    truth once again.

[bu]:    ./backup.md
[forks]: ./branching.wiki
[setup]: ./caps/admin-v-setup.md#apsu
[wflow]: ./concepts.wiki#workflow


<a id="reset"></a>
## Resetting the Repository

Extending from [the prior item](#syncall), you may correctly infer that
“[delete the project and download a fresh copy][x1597]” has no part in
the Fossil Way. Ideally, you should never find yourself forced into
desperate measures like this:(^Parsing the output of `fossil status` is
usually a mistake since it relies on a potentially unstable interface.
We make no guarantee that there will always be a line beginning with
“`repo`” and that it will be separated from the repository’s file name
by a colon. The simplified example above is also liable to become
confused by whitespace in file names.)


```
$ repo=$(fossil status | grep ^repo | cut -f2 -d:)
$ url=$(fossil remote)
$ fossil close             # Stop here and think if it warns you!
$ mv $repo ${repo}.old
$ fossil clone $url $repo
$ fossil open --force $repo
```

What, then, should you as a Git transplant do instead when you find
yourself reaching for “`git reset`”?

Since the correct answer to that depends on why you think it’s a good
solution to your immediate problem, we’ll take our motivating scenario
from the problem setup above, where we discussed Fossil’s [autosync]
feature.  Let us further say Alice’s pique results from a belief that
Bob’s commit is objectively wrong-headed and should be expunged
henceforth. Since Fossil goes out of its way to ensure that [commits are
durable][wdm], it should be no further surprise that there is no easier
method to reset Bob’s clone in favor of Alice’s than the above sequence
in Fossil’s command set. Except in extreme situations, we believe that
sort of thing is unnecessary.

Instead, Bob can say something like this:

```
fossil amend --branch MISTAKE --hide --close -m "mea culpa" tip
fossil up trunk
fossil push
```

Unlike in Git, the “`amend`” command doesn’t modify prior committed
artifacts. Bob’s first command doesn’t delete anything, merely tells
Fossil to hide his mistake from timeline views by inserting a few new
records into the local repository to change how the client interprets
the data it finds there henceforth.(^One to change the tag marking this
commit’s branch name to “`MISTAKE`,” one to mark that branch as hidden,
and one to close it to further commits.)

Bob’s second command switches his working directory back to the prior
commit on that branch. We’re presuming it was “`trunk`” for the sake of
the example, but it works for any parent branch name. The command works
because the name “`trunk`” now means something different to Fossil by
virtue of the first command.

Bob’s third command pushes the changes up to the central machine to
inform everyone else of his amendment.(^Amendments don’t autosync in
Fossil because they don’t change any previous commits, allowing the
other clones to continue working safely with their existing commit
hashes.)

In this scheme, Alice then needs to say “`fossil update trunk`” in order
to return her check-out’s parent commit to the previous version lest her
next attempted commit land atop this mistake branch. The fact that Bob
marked the branch as closed will prevent that from going through, cluing
Alice into what she needs to do to remedy the situation, but that merely
shows why it’s a better workflow if Alice makes the amendment herself:

```
fossil amend --branch MISTAKE --hide --close \
  -m "shunt Bob’s erroneous commit off" tip
fossil up trunk
fossil push
```

Then she can fire off an email listing Bob’s assorted failings and go
about her work. This asynchronous workflow solves the problem without
requiring explicit coordination with Bob. When he gets his email, he can
then say “`fossil up trunk`” himself, which by default will trigger an
autosync, pulling down Alice’s amendments and getting him back onto her
development track.

Remember that [branch names need not be unique](#btnames) in Fossil. You
are free to reuse this “`MISTAKE`” branch name as often as you need to.

[autosync]: #autosync
[x1597]:    https://xkcd.com/1597/


<a id="trunk"></a>
## The Main Branch Is Called "`trunk`"

In Fossil, the default name for the main branch
is "`trunk`".  The "`trunk`" branch in Fossil corresponds to the
"`master`" branch in stock Git or to [the “`main`” branch in GitHub][mbgh].

Because the `fossil git export` command has to work with both stock Git
and with GitHub, Fossil uses Git’s traditional default rather than
GitHub’s new default: your Fossil repo’s “trunk” branch becomes “master”
when [mirroring to GitHub][mirgh] unless you give the `--mainbranch`
option.

We do not know what happens on subsequent exports if you later rename
this branch on the GitHub side.

[mbgh]:  https://github.com/github/renaming
[mirgh]: ./mirrortogithub.md


<a id="unmanaged"></a>
## Status Does Not Show Unmanaged Files

The "`fossil status`" command shows you what files in your check-out have
been edited and scheduled for adding or removing at the next commit.
But unlike "`git status`", the "`fossil status`" command does not warn
you about unmanaged files in your local check-out.  There is a separate
"`fossil extras`" command for that.


<a id="rebase"></a>
## There Is No Rebase

Fossil does not support rebase, [on purpose][3].

This is a deliberate design decision that the Fossil community has
thought about carefully and discussed many times, resulting in the
linked document. If you are fond of rebase, you should read it carefully
before expressing your views: it not only answers many of the common
arguments in favor of rebase known at the time the document’s first
draft was written, it’s been revised multiple times to address less
common objections as well. Chances are not good that you are going to
come up with a new objection that we haven’t already considered and
addressed there.

There is only one sub-feature of `git rebase` that is philosophically
compatible with Fossil yet which currently has no functional equivalent.
We [covered this and the workaround for its lack](#comsplit) above.

[3]: ./rebaseharm.md


<a id="cdiff"></a>
## Colorized Diffs

When you run `git diff` on an ANSI X3.64 capable terminal, it uses color
to distinguish insertions, deletions, and replacements, but as of this
writing, `fossil diff` produces traditional uncolored [unified diff
format][udiff] output, suitable for producing a [patch file][pfile].

There are [many methods](./colordiff.md) for solving this.
Viewed this way, Fossil doesn’t lack colorized diffs, it simply has
*one* method where they *aren’t* colorized.

[pfile]: https://en.wikipedia.org/wiki/Patch_(Unix)
[udiff]: https://en.wikipedia.org/wiki/Diff#Unified_format


## <a id="show"></a> Showing Information About Commits

While there is no direct equivalent to Git’s “`show`” command, similar
functionality is present in Fossil under other commands:


#### <a id="patch"></a> Show a Patch for a Commit

    git show -p COMMIT_ID

…gives much the same output as

    fossil diff --checkin COMMIT_ID

…only without the patch email header. Git comes out of the [LKML] world,
where emailing a patch is a normal thing to do. Fossil is [designed for
cohesive teams][devorg] where drive-by patches are rarer.

You can use any of [Fossil’s special check-in names][scin] in place of
the `COMMIT_ID` in this and later examples. Fossil docs usually say
“`VERSION`” or “`NAME`” where this is allowed, since the version string
or name might not refer to a commit ID, but instead to a forum post, a
wiki document, etc. For instance, the following command answers the question “What did
I just commit?”

    fossil diff --checkin tip

…or equivalently using a different symbolic commit name:

    fossil diff --from prev

[devorg]: ./fossil-v-git.wiki#devorg
[LKML]:   https://lkml.org/


#### <a id="cmsg"></a> Show a Specific Commit Message

    git show -s COMMIT_ID


…is

    fossil time -n 1 COMMIT_ID

…or with a shorter, more obvious command, though with more verbose output:

    fossil info COMMIT_ID

The `fossil info` command isn’t otherwise a good equivalent to
`git show`; it just overlaps its functionality in some areas. Much of
what’s missing is present in the corresponding [`/info` web
view][infow], though.


#### <a id="dstat"></a> Diff Statistics

Fossil’s closest internal equivalent to commands like
`git show --stat` is:

    fossil diff -i --from 2020-04-01 --numstat

The `--numstat` output is a bit cryptic, so we recommend delegating
this task to [the widely-available `diffstat` tool][dst], which gives
a histogram in its default output mode rather than bare integers:

    fossil diff -i -v --from 2020-04-01 | diffstat

We gave the `-i` flag in both cases to force Fossil to use its internal
diff implementation, bypassing [your local `diff-command` setting][dcset]
since the `--numstat` option has no effect when you have an external diff
command set.

If you leave off the `-v` flag in the second example, the `diffstat`
output won’t include info about any newly-added files.

[dcset]: https://fossil-scm.org/home/help?cmd=diff-command
[dst]:   https://invisible-island.net/diffstat/diffstat.html


<a id="btnames"></a>
## Branch and Tag Names

Fossil has no special restrictions on the names of tags and branches,
though you might want to keep [Git's tag and branch name restrictions][gcrf]
in mind if you plan on [mirroring your Fossil repository to GitHub][mirgh].

Fossil does not require tag and branch names to be unique.  It is
common, for example, to put a “`release`” tag on every release for a
Fossil-hosted project. This does not create a conflict in Fossil, since
Fossil resolves the ambiguity in a predictable way: the newest match
wins. Therefore, “`fossil up release`” always gets you the current
release in a project that uses this tagging convention.

[The `fossil git export` command][fge] squashes repeated tags down to a
single instance to avoid confusing Git, exporting only the newest tag,
emulating Fossil’s own ambiguity resolution rule as best it can within
Git’s limitations.

[fge]:  /help?cmd=git
[gcrf]: https://git-scm.com/docs/git-check-ref-format




<a id="cpickrev"></a>
## Cherry-Picking and Reverting Commits

Git’s separate "`git cherry-pick`" and “`git revert`” commands are
options to the [`fossil merge` command][merge]: `--cherrypick` and
`--backout`, respectively. We view this as sensible, since these are
both merge operations, and the two actions differ only in direction.

Unlike in Git, the Fossil file format remembers cherrypicks and backouts
and can later show them as dashed lines on the graphical timeline.

[merge]: /help?cmd=merge



<a id="mvrm"></a>
## File Moves and Renames Are Soft by Default

The "[`fossil mv`][mv]" and "[`fossil rm`][rm]" commands work like they
do in CVS in that they schedule the changes for the next commit by
default: they do not actually rename or delete the files in your
check-out.

If you don’t like that default, you can change it globally:

    fossil setting --global mv-rm-files 1

Now these commands behave like in Git in any Fossil repository where
this setting hasn’t been overridden locally.

If you want to keep Fossil’s soft `mv/rm` behavior most of the time, you
can cast it away on a per-command basis:

    fossil mv --hard old-name new-name

[mv]: /help?cmd=mv
[rm]: /help?cmd=rm


----


## <a id="cvdate" name="cs1"></a> Case Study 1: Checking Out a Version by Date

Let’s get into something a bit more complicated: a case study showing
how the concepts lined out above cause Fossil to materially differ in
day-to-day operation from Git.

Why would you want to check out a version of a project by date?  Perhaps
your customer gave you a vague bug report referencing only a
date rather than a version. Or, you may be poking semi-randomly through
history to find a “good” version to anchor the start point of a
[`fossil bisect`][fbis] operation.

My search engine’s first result for “git checkout by date” is [this
highly-upvoted accepted Stack Overflow answer][gcod]. The first command
it gives is based on Git’s [`rev-parse` feature][grp]:

    git checkout master@{2020-03-17}

There are a number of weaknesses in this command. From least to most
critical:

1.  It’s a bit cryptic. Leave off the refname or punctuation, and it
    means something else. You cannot simplify the cryptic incantation in
    the typical use case.

2.  A date string in Git without a time will be interpreted as
    “[at the local wall clock time on the given date][gapxd],” so the
    command means something different from one second to the next! This
    can be a serious problem if there are multiple commits on that date, because
    the command will give different results depending on the time of
    day you run it.

3.  It gives misleading output if there is no close match for the date
    in the local [reflog]. It starts out empty after a fresh clone, and
    while it does build up as you use that clone, Git [automatically
    prunes][gle] the reflog to 90 days of history by default. This means
    the command above can give different results from one machine to the
    next, or even from one day to the next on the same clone.

    The command won’t fail outright if the reflog can’t resolve the
    given date: it simply gives the closest commit it can come up with,
    even if it’s months or years out from your target! Sometimes it
    gives a warning about the reflog not going back far enough to give a
    useful result, and sometimes it doesn’t. If you’re on a fresh clone,
    you are likely to get the “tip” commit’s revision ID no matter what
    date value you give.

    Git tries its best, but because it’s working from a purgeable and
    possibly-stale local cache, you cannot trust its results.

Consequently, we cannot recommend this command at all. It’s unreliable even in the
best case.

That same Stack Overflow answer therefore goes on to recommend an
entirely different command:

    git checkout $(git rev-list -n 1 --first-parent --before="2020-03-17" master)

We believe you get such answers to Git help requests in part
because of its lack of an always-up-to-date [index into its log](#log) and in
part because of its “small tools loosely joined” design philosophy. This
sort of command is therefore composed piece by piece:

<p style="text-align:center">◆  ◆  ◆</p>

“Oh, I know, I’ll search the rev-list, which outputs commit IDs by
parsing the log backwards from `HEAD`! Easy!”

    git rev-list --before=2020-03-17

“Blast! Forgot the commit ID!”

    git rev-list --before=2020-03-17 master

“Double blast! It just spammed my terminal with revision IDs! I need to
limit it to the single closest match:

    git rev-list -n 1 --before=2020-03-17 master

“Okay, it gives me a single revision ID now, but is it what I’m after?
Let’s take a look…”

    git show $(git rev-list -n 1 --before=2020-03-17 master)

“Oops, that’s giving me a merge commit, not what I want.
Off to search the web… Okay, it says I need to give either the
`--first-parent` or `--no-merges` flag to show only regular commits,
not merge-commits. Let’s try the first one:”

    git show $(git rev-list -n 1 --first-parent --before=2020-03-17 master)

“Better. Let’s check it out:”

    git checkout $(git rev-list -n 1 --first-parent --before=2020-03-17 master)

“Success, I guess?”

<p style="text-align:center">◆  ◆  ◆</p>

This vignette is meant to explain some of Git’s popularity: it rewards
the sort of people who enjoy puzzles, many of whom are software
developers and thus need a tool like Git. Too bad if you’re just a
normal user.

And too bad if you’re a Windows user who doesn’t want to use [Git
Bash][gbash], since neither of the stock OS command shells have a
command interpolation feature needed to run that horrid command.

This alternative command still has weakness #2 above: if you run the
second `git show` command above on [Git’s own repository][gitgh], your
results may vary because there were four non-merge commits to Git on the
17th of March, 2020.

You may be asking with an exasperated huff, “What is your *point*, man?”
The point is that the equivalent in Fossil is simply:

    fossil up 2020-03-17

…which will *always* give the commit closest to midnight UTC on the 17th
of March, 2020, no matter whether you do it on a fresh clone or a stale
one.  The answer won’t shift about from one clone to the next or from
one local time of day to the next. We owe this reliability and stability
to three Fossil design choices:

*  Parse timestamps from all commits on clone into a local commit index,
   then maintain that index through subsequent commits and syncs.

*  Use an indexed SQL `ORDER BY` query to match timestamps to commit
   IDs for a fast and consistent result.

*  Round incomplete timestamp strings up using [rules][frud] consistent across
   computers and local time of day.

[frud]:   https://fossil-scm.org/home/file/src/name.c?ci=d2a59b03727bc3&ln=122-141
[gbash]:  https://appuals.com/what-is-git-bash/
[gapxd]:  https://github.com/git/git/blob/7f7ebe054a/date.c#L1298-L1300
[gcod]:   https://stackoverflow.com/a/6990682/142454
[gdh]:    https://www.git-tower.com/learn/git/faq/detached-head-when-checkout-commit/
[gitgh]:  https://github.com/git/git/
[gle]:    https://git-scm.com/docs/git-reflog#_options_for_expire
[gmc]:    https://github.com/git/git/commit/67b0a24910fbb23c8f5e7a2c61c339818bc68296
[grp]:    https://git-scm.com/docs/git-rev-parse
[reflog]: https://git-scm.com/docs/git-reflog

----

## <a id="morigin" name="cs2"></a> Case Study 2: Multiple "origin" Servers

Now let us consider a common use case at the time of this writing — during the
COVID-19 pandemic — where you’re working from home a lot, going into the
office one part-day a week only to do things that have to be done
on-site at the office.  Let us also say you have no remote
access back into the work LAN, such as because your site IT is paranoid
about security. You may still want off-machine backups of your commits
while working from home,
so you need the ability to quickly switch between the “home” and
“work” remote repositories, with your laptop acting as a kind of
[sneakernet][sn] link between the big development server at the office
and your family’s home NAS.

#### Git Method

We first need to clone the work repo down to our laptop, so we can work on it
at home:

    git clone https://dev-server.example.com/repo
    cd repo
    git remote rename origin work

The last command is optional, strictly speaking. We could continue to
use Git’s default name for the work repo’s origin — sensibly enough
called “`origin`” — but it makes later commands harder to understand, so
we rename it here. This will also make the parallel with Fossil easier
to draw.

The first time we go home after this, we have to reverse-clone the work
repo up to the NAS:

    ssh my-nas.local 'git init --bare /SHARES/dayjob/repo.git'
    git push --all ssh://my-nas.local//SHARES/dayjob/repo.git

Realize that this is carefully optimized down to these two long
commands. In practice, we’d expect a user typing these commands by hand from memory
to need to give four or more commands here instead.
Packing the “`git init`” call into the “`ssh`” call is something more
often done in scripts and documentation examples than done interactively,
which then necessitates a third command before the push, “`exit`”.
There’s also a good chance that you’ll forget the need for the `--bare`
option here to avoid a fatal complaint from Git that the laptop can’t
push into a non-empty repo. If you fall into this trap, among the many
that Git lays for newbies, you have to nuke the incorrectly initted
repo, search the web or Git man pages to find out about `--bare`, and try again.

Having navigated that little minefield,
we can tell Git that there is a second origin, a “home” repo in
addition to the named “work” repo we set up earlier:

    git remote add home ssh://my-nas.local//SHARES/dayjob/repo.git
    git config master.remote home

We don’t have to push or pull because the remote repo is a complete
clone of the repo on the laptop at this point, so we can just get to
work now, committing along the way to get our work safely off-machine
and onto our home NAS, like so:

    git add
    git commit
    git push

We didn’t need to give a remote name on the push because we told it the
new upstream is the home NAS earlier.

Now Friday comes along, and one of your office-mates needs a feature
you’re working on. You agree to come into the office later that
afternoon to sync up via the dev server:

    git push work master      # send your changes from home up
    git pull work master      # get your coworkers’ changes

Alternately, we could add “`--set-upstream/-u work`” to the first
command if we were coming into work long enough to do several Git-based things, not just pop in and sync.
That would allow the second to be just “`git pull`”, but the cost is
that when returning home, you’d have to manually reset the upstream
again.

This example also shows a consequence of that fact that
[Git doesn’t sync branch names](#syncall): you have to keep repeating
yourself like an obsequious supplicant: “Master, master.” Didn’t we
invent computers to serve humans, rather than the other way around?


#### Fossil Method

Now we’re going to do the same thing using Fossil, with
the commands arranged in blocks corresponding to those above for comparison.

We start the same way, cloning the work repo down to the laptop:

    fossil clone https://dev-server.example.com/repo
    cd repo
    fossil remote add work https://dev-server.example.com/repo

We’ve chosen the new “`fossil clone URI`” syntax rather than separate
`clone` and `open` commands to make the parallel with Git clearer. [See
above](#mwd) for more on that topic.

Our [`remote` command][rem] is longer than the Git equivalent because
Fossil currently has no short command
to rename an existing remote. Worse, unlike with Git, we can’t just keep
using the default remote name because Fossil uses that slot in its
configuration database to store the *current* remote name, so on
switching from work to home, the home URL will overwrite the work URL if
we don’t give it an explicit name first.

Although the Fossil commands are longer, so far, keep it in perspective:
they’re one-time setup costs,
easily amortized to insignificance by the shorter day-to-day commands
below.

On first beginning to work from home, we reverse-clone the Fossil repo
up to the NAS:

    rsync repo.fossil my-nas.local:/SHARES/dayjob/

Now we’re beginning to see the advantage of Fossil’s simpler model,
relative to the tricky “`git init && git push`” sequence above.
Fossil’s alternative is almost impossible to get
wrong: copy this to that.  *Done.*

We’re relying on the `rsync` feature that creates up to one level of
missing directory (here, `dayjob/`) on the remote. If you know in
advance that the remote directory already exists, you could use a
slightly shorter `scp` command instead. Even with the extra 2 characters
in the `rsync` form, it’s much shorter because a Fossil repository is a
single SQLite database file, not a tree containing a pile of assorted
files.  Because of this, it works reliably without any of [the caveats
inherent in using `rsync` to clone a Git repo][grsync].

Now we set up the second remote, which is again simpler in the Fossil
case:

    fossil remote add home ssh://my-nas.local//SHARES/dayjob/repo.fossil
    fossil remote home

The first command is nearly identical to the Git version, but the second
is considerably simpler. And to be fair, you won’t find the
“`git config`” command above in all Git tutorials. The more common
alternative we found with web searches is even longer:
“`git push --set-upstream home master`”.

Where Fossil really wins is in the next step, making the initial commit
from home:

    fossil ci

It’s one short command for Fossil instead of three for Git — or two if
you abbreviate it as “`git commit -a && git push`” — because of Fossil’s
[autosync] feature and deliberate omission of a
[staging feature](#staging).

The “Friday afternoon sync-up” case is simpler, too:

    fossil remote work
    fossil sync

Back at home, it’s simpler still: we may be able to do away with the second command,
saying just “`fossil remote home`” because the sync will happen as part
of the next commit, thanks once again to Fossil’s autosync feature. If
the working branch now has commits from other developers after syncing
with the central repository, though, you’ll want to say “`fossil up`” to
avoid creating an inadvertent fork in the branch.

(Which is easy to fix if it occurs: `fossil merge` without arguments
means “merge open tips on the current branch.”)

[grsync]: https://stackoverflow.com/q/1398018/142454
[qs]:     ./quickstart.wiki
[shwmd]:  ./fossil-v-git.wiki#checkouts
[sn]:     https://en.wikipedia.org/wiki/Sneakernet
