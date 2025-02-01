# Limitations On Git Mirrors

The "<tt>[fossil git export](/help?cmd=git)</tt>" command can be used to
mirror a Fossil repository to Git.
([Setup instructions](./mirrortogithub.md) and an
[example](https://github.com/drhsqlite/fossil-mirror).)
But the export to Git is not perfect. Some information is lost during
export due to limitations in Git.  This page describes what content of
Fossil is not included in an export to Git.

## (1) Wiki, Tickets, Technotes, Forum

Git only supports version control. The additional features of Fossil such
as Wiki, Tickets, Technotes, and the Forum are not supported in Git,
so those features are not included in an export.

Third-party Git based tooling may add some of these features (e.g.
GitHub, GitLab) but because their data are not stored in the Git
repository, there is no single destination for Fossil to convert its
equivalent data *to*. For instance, Fossil tickets do not become GitHub
issues, because that is a proprietary feature of GitHub separate from
Git proper, stored outside the repository on the GitHub servers.

You can also see the problem in its inverse case: you do not get a copy
of your GitHub issues when cloning the Git repository. You *do* get the
Fossil tickets, wiki, forum posts, etc. when cloning a remote Fossil
repo.

## (2) Cherrypick Merges

The Git client supports cherrypick merges but does not record the
cherrypick parent(s).

Fossil tracks cherrypick merges in its repository and displays
cherrypicks in its timeline. (As an example, the dashed lines
[here](/timeline?c=0a9f12ce6655b7a5) are cherrypicks.) Because Git does
not have a way to represent this same information in its repository, the
history of Fossil cherrypicks cannot be exported to Git, only their
direct effects on the managed file data.

## (3) Named Branches

Git has only limited support for named branches.  Git identifies the head
check-in of each branch.  Depending on the check-in graph topology, this
is sufficient to infer the branch for many historical check-ins as well.
However, complex histories with lots of cross-merging
can lead to ambiguities.  Fossil keeps
track of historical branch names unambiguously, 
but the extra details about branch names that Fossil keeps
at hand cannot be exported to Git.

An example of the kinds of ambiguities that arise when branch names
are not tracked is a "diamond-merge" history. In a diamond-merge, a
long-running development branch merges enhancements from trunk from
time to time and also periodically merges the development changes back
to trunk at moments when the branch is stable.
An example of diamond-merge in the Fossil source tree itself
can be seen at on the [bv-corrections01 branch](/timeline?r=bv-corrections01).
The distinction between checkins on the branch and checkins on trunk would
be lost in Git, which does not track branches for individual checkins,
and so you cannot (easily) tell which checkins are part of the branch and
which are part of trunk in a diamond-merge history on Git.  For that
reason, diamond-merge histories are considered an anti-pattern in Git
and the usual recommendation for Git users is to employ
[rebase](./rebaseharm.md) to clean the history up.  The point here is
that if your project has a diamond-merge history that shows up cleanly
in Fossil, it will export to Git and still be technically correct, but 
the history display might be a jumbled mess that is difficult for humans
to comprehend.

## (4) Non-unique Tags

Git requires tags to be unique: each tag must refer to exactly one
check-in.  Fossil does not have this restriction, and so it is common
in Fossil to tag multiple check-ins with the same name.  For example,
it is common in Fossil to tag each check-in creating a release both
with a unique version tag *and* a common tag like "release"
so that all historical releases can be found at once.
([Example](/timeline?t=release).)

Git does not allow this.  The "release" tag must refer to just one
check-in.  The work-around is that the non-unique tag in the Git export is 
made to refer to only the most recent check-in with that tag.

This is why the ["release" tag view][ghrtv] in the GitHub mirror of this
repository shows only the latest release version; contrast the prior
example. Both URLs are asking the repository the same question, but
because of Git's relatively impoverished data model, it cannot give the
same answer that Fossil does.

[ghrtv]: https://github.com/drhsqlite/fossil-mirror/tree/release

## (5) Amendments To Check-ins

Check-ins are immutable in both Fossil and Git.
However, Fossil has a mechanism by which tags can be added to
its repository to provide after-the-fact corrections to prior check-ins.

For example, tags can be added to check-ins that correct typos in the
check-in comment.  The original check-in is immutable and so the
original comment is preserved in addition to the correction. But
software that displays the check-ins knows to look for the comment-change
tag and if present displays the corrected comment rather than the original.
([Example](/info/8ed91bbe44d0d383) changing the typo "os" into "so".)

Git has no mechanism for providing corrections or clarifications to
historical check-ins.

When exporting from Fossil to Git, the latest corrections to a Fossil check-in
are used to generate the corresponding Git check-in.  But once the Git
check-in has been created, any subsequent corrections are omitted as there
is no way to transfer them to Git.
