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

## There Is No Staging Area

Fossil omits the "Git index" or "staging area" concept.  When you
type "`fossil commit`" _all_ changes in your check-out are committed,
automatically.  There is no need for the "-a" option as with Git.

If you only want to commit just some of the changes, you can list the names
of the files you want to commit as arguments, like this:

        fossil commit src/main.c doc/readme.md

## Create Branches After-The-Fact

Fossil perfers that you create new branches when you commit using
the "`--branch` _BRANCH-NAME_" command-line option.  For example:

        fossil commit --branch my-new-branch

It is not necessary to create branches ahead of time, as in Git, though
that is allowed using the "`fossil branch new`" command, if you
prefer.  Fossil also allows you to move a check-in to a different branch
*after* you commit it, using the "`fossil amend`" command.
For example:

        fossil amend current --branch my-new-branch

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

## Syncing Is All-Or-Nothing

Fossil does not support the concept of syncing, pushing, or pulling
individual branches.  When you sync/push/pull in Fossil, you sync/push/pull
everything - all branches, all wiki, all tickets, all forum posts,
all tags, all technotes - everything.

## The Main Branch Is Called "`trunk`", not "`master`"

In Fossil, the traditional name and the default name for the main branch
is "`trunk`".  The "`trunk`" branch in Fossil corresponds to the
"`master`" branch in Git.

These naming conventions are so embedded in each system, that the
"trunk" branch name is automatically translated to "master" when
a [Fossil repo is mirrored to GitHub][6].

[6]: ./mirrortogithub.md

## The "`fossil status`" Command Does Not Show Unmanaged Files

The "`fossil status`" command shows you what files in your check-out have
been edited and scheduled for adding or removing at the next commit.
But unlike "`git status`", the "`fossil status`" command does not warn
you about unmanaged files in your local check-out.  There is a separate
"`fossil extras`" command for that.

## There Is No Rebase

Fossil does not support rebase.
This is a [deliberate design decision][3] that has been thoroughly,
carefully, and throughtfully discussed, many times.  If you are fond
of rebase, you should read the [Rebase Considered Harmful][3] document
carefully before expressing your views.

[3]: ./rebaseharm.md

## Branch and Tag Names

Fossil has no special restrictions on the names of tags and branches,
though you might want to to keep [Git's tag and branch name restrictions][4]
in mind if you plan on mirroring your Fossil repository to GitHub.

[4]: https://git-scm.com/docs/git-check-ref-format

Fossil does not require tag and branch names to be unique.  It is
common, for example, to put a "`release`" tag on every release for a
Fossil-hosted project.

## Only One "origin" At A Time

A Fossil repository only keeps track of one "origin" server at a time.
If you specify a new "origin" it forgets the previous one.  Use the
"`fossil remote`" command to see or change the "origin".

Fossil uses a very different sync protocol than Git, so it isn't as
important for Fossil to keep track of multiple origins as it is with
Git.  So only having a single origin has never been a big enough problem
in Fossil that somebody felt the need to extend it.

Maybe we will add multiple origin support to Fossil someday.  Patches
are welcomed if you want to have a go at it.

## Cherry-pick Is An Option To The "merge" Command

In Git, "`git cherry-pick`" is a separate command.
In Fossil, "`fossil merge --cherrypick`" is an option on the merge
command.  Otherwise, they work mostly the same.

Except, the Fossil file format remembers cherrypicks and actually
shows them as dashed lines on the graphical DAG display, whereas
there is no provision for recording cherry-picks in the Git file
format, so you have to talk about the cherry-pick in the commit
comment if you want to remember it.

## The "`fossil mv`" and "`fossil rm`" Commands Do Not Actually Rename Or Delete The Files (by default)

By default,
the "`fossil mv`" and "`fossil rm`" commands work like they do in CVS in
that they schedule the changes for the next commit, but do not actually
rename or delete the files in your check-out.  You can to add the "--hard"
option to also changes the files in your check-out.
If you run

         fossil setting --global mv-rm-files 1

it makes a notation in your per-user "~/.fossil" settings file so that
the "--hard" behavior becomes the new default.
