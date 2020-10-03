# Hints For Users With Prior Git Experience

This document is a semi-random collection of hints intended to help
new users of Fossil who have had prior exposure to Git.  In other words,
this document tries to describe the differences in how Fossil works 
from the perspective of Git users.


## Help Improve This Document

If you have a lot of prior Git experience, and you are new to Fossil
and are struggling with some concepts, please ask for help on the
[Fossil Forum][1].  The people who write this document are intimately
familiar with Fossil and less familiar with Git.  It is difficult for
us to anticipate the perspective of people who are initimately familiar
with Git and less familiar with Fossil.  Asking questions on the Forum
will help us to improve the document.

[1]:  https://fossil-scm.org/forum

Specific suggestions on how to improve this document are also welcomed,
of course.


<a id="mwd"></a>
## Repositories And Checkouts Are Distinct

A repository and a check-out are distinct concepts in Fossil, whereas
the two are often conflated with Git.  A repository is a database in
which the entire history of a project is stored.  A check-out is a
directory hierarchy that contains a snapshot of your project that you
are currently working on.  See [detailed definitions][2] for more
information.  With Git, the repository and check-out are closely
related - the repository is the contents of the "`.git`" subdirectory
at the root of your check-out.  But with Fossil, the repository and
the check-out are completely separate.  A Fossil repository can reside
in the same directory hierarchy with the check-out as with Git, but it
is more common to put the repository in a separate directory.

[2]: ./whyusefossil.wiki#definitions

Fossil repositories are a single file, rather than being a directory
hierarchy as with the "`.git`" folder in Git.  The repository file
can be named anything you want, but it is best to use the "`.fossil`"
suffix.  Many people choose to gather all of their Fossil repositories
in a single directory on their machine, such as "`~/Fossils`" or
"`C:\Fossils`".  This can help humans to keep their repositories
organized, but Fossil itself doesn't really care.

Because Fossil cleanly separates the repository from the check-out, it
is routine to have multiple check-outs from the same repository.  Each
check-out can be on a separate branch, or on the same branch.  Each
check-out operates independently of the others.

Each Fossil check-out contains a file (usually named "`.fslckout`" on
unix or "`_FOSSIL_`" on Windows) that keeps track of the status of that
particular check-out and keeps a pointer to the repository.  If you
move or rename the repository file, the check-outs won't be able to find 
it and will complain.  But you can freely move check-outs around without
causing any problems.


<a id="staging"></a>
## There Is No Staging Area

Fossil omits the "Git index" or "staging area" concept.  When you
type "`fossil commit`" _all_ changes in your check-out are committed,
automatically.  There is no need for the "-a" option as with Git.

If you only want to commit just some of the changes, you can list the names
of the files you want to commit as arguments, like this:

        fossil commit src/main.c doc/readme.md


<a id="bneed"></a>
## Create Branches At Point Of Need, Rather Than Ahead of Need

Fossil prefers that you create new branches as part of the first commit
on that branch:

       fossil commit --branch my-new-branch

If that commit is successful, your local checkout directory is then
switched to the tip of that branch, so subsequent commits don’t need the
“`--branch`” option. You have to switch back to the parent branch
explicitly, as with

       fossil update trunk       # return to parent, “trunk” in this case

Fossil does also support the Git style, creating the branch ahead of
need:

       fossil branch new my-new-branch
       fossil update my-new-branch
       ...work on first commit...
       fossil commit

This is more verbose, but it has the same effect: put the first commit
onto `my-new-branch` and switch the checkout directory to that branch so
subsequent commits are descendants of that initial branch commit.

Fossil also allows you to move a check-in to a different branch
*after* you commit it, using the "`fossil amend`" command.
For example:

        fossil amend current --branch my-new-branch

(“current” is one of the [special check-in names][scin] in Fossil. See
that document for the many other names you can give to “`amend`”.)

[scin]: ./checkin_names.wiki


<a id="autosync"></a>
## Autosync

Fossil has a feature called "[autosync][5]".  Autosync defaults on.
When autosync is enabled, Fossil automatically pushes your changes
to the remote server whenever you "`fossil commit`".  It also automatically
pulls all remote changes down to your local repository before you
"`fossil update`".

[5]: ./concepts.wiki#workflow

Autosync provides most of the advantages of a centralized version
control system while retaining the advantages of distributed version
control.  Your work stays synced up with your coworkers at all times.
If your local machine dies catastrophically, you haven't lost any
(committed) work.  But you can still work and commit while off network,
with changes resyncing automatically when you get back on-line.


<a id="syncall"></a>
## Syncing Is All-Or-Nothing

Fossil does not support the concept of syncing, pushing, or pulling
individual branches.  When you sync/push/pull in Fossil, you sync/push/pull
everything: all branches, all wiki, all tickets, all forum posts,
all tags, all technotes… Everything.

Furthermore, branch *names* sync automatically in Fossil, not just the
content of those branches. This means this common Git command:

        git push origin master

is simply this in Fossil:

        fossil push

Fossil doesn’t need to be told what to push or where to push it: it just
keeps using the same remote server URL and branch name you gave it last,
until you tell it to do something different.


<a id="trunk"></a>
## The Main Branch Is Called "`trunk`"

In Fossil, the traditional name and the default name for the main branch
is "`trunk`".  The "`trunk`" branch in Fossil corresponds to the
"`master`" branch in stock Git or the "`main`" branch in GitHub.

Because the `fossil git export` command has to work with both stock Git
and with GitHub, Fossil uses Git’s default: your Fossil repo’s “trunk”
branch becomes “master” on GitHub, not “main,” as in new GitHub repos.
It is not known what happens on subsequent exports if you
[later rename it][ghmain].

[6]: ./mirrortogithub.md
[ghmain]: https://github.com/github/renaming


<a id="unmanaged"></a>
## The "`fossil status`" Command Does Not Show Unmanaged Files

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

[3]: ./rebaseharm.md


<a id="btnames"></a>
## Branch and Tag Names

Fossil has no special restrictions on the names of tags and branches,
though you might want to keep [Git's tag and branch name restrictions][4]
in mind if you plan on mirroring your Fossil repository to GitHub.

[4]: https://git-scm.com/docs/git-check-ref-format

Fossil does not require tag and branch names to be unique.  It is
common, for example, to put a "`release`" tag on every release for a
Fossil-hosted project. This does not create a conflict in Fossil, since
Fossil resolves such conflicts in a predictable way: the newest match
wins. Therefore, “`fossil up release`” always gets you the current
release in a project that uses this tagging convention.


<a id="cpickrev"></a>
## Cherry-Picking and Reverting Commits

Git’s separate "`git cherry-pick`" and “`git revert`” commands are
options to the [`fossil merge` command][merge]: `--cherrypick` and
`--backout`, respectively.

Unlike in Git, the Fossil file format remembers cherrypicks and backouts
and can later show them as dashed lines on the graphical timeline.

[merge]: /help?cmd=merge



<a id="mvrm"></a>
## File Moves And Renames Are Soft By Default

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


<a id="morigin"></a>
## Multiple "origin" Servers

In this final section of the document, we’ll go into a lot more detail
to illustrate the points above, not just give a quick summary of this
single difference.

Consider a common use case — at the time of this writing, during the
COVID-19 pandemic — where you’re working from home a lot, going into the
office maybe one part-day a week.  Let us also say you have no remote
access back into the work LAN, such as because your site IT is paranoid
about security. You may still want off-machine backups of your commits,
so what you want is the ability to quickly switch between the “home” and
“work” remote repositories, with your laptop acting as a kind of
[sneakernet][sn] link between the big development server at the office
and your family’s home NAS.

### Git Method

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

Now we can tell Git that there is a second origin, a “home” repo in
addition to the named “work” repo we set up earlier:

        git remote add home ssh://my-nas.local//SHARES/dayjob/repo.git
        git config master.remote home

We don’t have to push or pull because the remote repo is a complete
clone of the repo on the laptop at this point, so we can just get to
work now, committing along the way to get our work safely off-machine
and onto the NAS, like so:

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
yourself, “master, master.”


### Fossil Method

Now we’re going to do the same thing as above using Fossil. We’ve broken
the commands up into blocks corresponding to those above for comparison.
We start the same way, cloning the work repo down to the laptop:

        mkdir repo
        cd repo
        fossil open https://dev-server.example.com/repo
        fossil remote add work https://dev-server.example.com/repo

Unlike Git, Fossil’s “clone and open” feature doesn’t create the
directory for you, so we need an extra `mkdir` call here that isn’t
needed in the Git case. This is an indirect reflection of Fossil’s
[multiple working directories](#mwd) design philosophy: its
[`open` command][open] requires that you either issue it in an empty
directory or one containing a prior closed checkout. In exchange for
this extra command, we get the advantage of Fossil’s
[superior handling][shwmd] of multiple working directories. To get the
full power of this feature, you’d switch from the “`fossil open URI`”
command form to the separate clone-and-open form shown in
[the quick start guide][qs], which adds one more command.

We can’t spin the longer final command as a trade-off giving us extra
power, though: the simple fact is, Fossil currently has no short command
to rename an existing remote. Worse, unlike with Git, we can’t just keep
using the default remote name because Fossil uses that slot in its
configuration database to store the *current* remote name, so on
switching from work to home, the home URL will overwrite the work URL if
we don’t give it an explicit name first.

Keep these costs in perspective, however: they’re one-time setup costs,
easily amortized to insignificance by the shorter day-to-day commands
below.

On first beginning to work from home, we reverse-clone the Fossil repo
up to the NAS:

        rsync repo.fossil my-nas.local:/SHARES/dayjob/

Now we’re beginning to see the advantage of Fossil’s simpler model,
relative the tricky “`git init && git push`” sequence above. It’d be
four commands instead of two if you weren’t clever enough to pack the
init into an `ssh` command, and unless you’re well versed with Git,
you’ll probably forget to give the `--bare` option the first time,
adding more commands as you blow the first attempt away and try again.
Contrast the Fossil alternative, which is almost impossible to get
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
is considerably simpler. And to be fair, you won’t find that second
command in all Git tutorials: the more common one we found with web
searches is “`git push --set-upstream home master`”. Not only is it
longer, it’s wasteful to do it that way in the example above because
we’re working with a just-created remote clone of the local repo, so
doing this via “push” makes Git do pointless extra work.

Where Fossil really wins is in the next step, making the initial commit
from home:

        fossil ci

It’s one short command for Fossil instead of three for Git — or two if
you abbreviate it as “`git commit -a && git push`” — because of Fossil’s
[autosync feature](#autosync) feature and deliberate omission of a
[staging feature](#staging).

The “Friday afternoon sync-up” case is simpler, too:

        fossil remote work
        fossil sync

Back at home, it’s even simpler: we can do away with the second command,
saying just “`fossil remote home`” because the sync will happen as part
of the next commit, thanks once again to Fossil’s autosync feature.

[grsync]: https://stackoverflow.com/q/1398018/142454
[open]:   /help?cmd=open
[qs]:     ./quickstart.wiki
[shwmd]:  ./fossil-v-git.wiki#checkouts
[sn]:     https://en.wikipedia.org/wiki/Sneakernet
