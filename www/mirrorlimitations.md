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
as Wiki, Tickets, Technotes, and the Forum are not supported in Git and
so those features are not included in an export.

## (2) Cherrypick Merges

The Git client supports cherrypick merges but does not remember them.
In other words, Git does not record a history of cherrypick merges
in its blockchain.

Fossil tracks cherrypick merges in its blockchain and display cherrypicks
(as dashed lines) in its timeline ([example](/timeline?c=0a9f12ce6655b7a5)).
But history information of cherrypicks cannot be exported to Git because
there is no way to represent it in the Git.

## (3) Named Branches

Git has only limited support for named branches.  Git identifies the head
check-in of each branch.  Depending on the check-in graph topology, this
is sufficient to infer the branch for many historical check-ins as well.
However, complex histories with lots of cross-merging
can lead to ambiguities.  Fossil keeps
track of historical branch names unambiguously. 
But the extra details about branch names that Fossil keeps
at hand cannot be exported to Git.

## (4) Non-unique Tags

Git requires tags to be unique.  Each tag must refer to exactly one
check-in.  Fossil does not have this restriction, and so it is common
in Fossil to tag multiple check-ins with the same name.  For example,
it is common in Fossil to tag every release check-in with the "release"
tag, so that all historical releases can be found all at once.
([example](/timeline?t=release))

Git does not allow this.  The "release" tag must refer to just one
check-in.  The work-around is that the non-unique tag in the Git export is 
made to refer to only the most recent check-in with that tag.

## (5) Amendments To Check-ins

Check-ins are immutable in both Fossil and Git.
However, Fossil has a mechanism by which tags can be added
its blockchain to provide after-the-fact corrections to prior check-ins.

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
