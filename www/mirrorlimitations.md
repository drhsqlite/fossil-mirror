# Limitations On Git Mirrors

The "<tt>[fossil git export](/help?cmd=git)</tt>" command can be used to
mirror a Fossil repository to Git.
([Setup instructions](./mirrortogithub) and an
[example](https://github.com/drhsqlite/fossil-mirror).)
But the export to Git is not perfect. Some information is lost during
export due to limitations in Git.  This page describes what content of
Fossil is not included in an export to Git.

## (1) Wiki, Tickets, Technotes, Forum

Git only supports version control. The additional features of Fossil such
as Wiki, Tickets, Technotes, and the Forum are not supported in Git and
so those features are not included in an export.

## (2) Cherrypick Merges

Git supports cherrypick merging, but the fact that a cherrypick merge occurred
is not recorded in the Git blockchain.  There is no way to record a cherrypick
merge in the low-level Git file format.

Fossil does track cherrypick merges in its low-level file format, but that
information must be discarded when exporting to Git as there is no way to
represent it in Git.

## (3) Named Branches

Git has only limited support for named branches.  Git identifies the head
check-in of each branch.  Depending on the check-in graph topology, this
is sufficient to infer the branch for many historical check-ins as well.
However, for complex histories with lots of cross-merging
the branch names for historical check-ins can become ambiguious.  Fossil keeps
track of historical branch names unambigously.  But some of this information
can go missing when exporting to Git.

## (4) Non-unique Tags

Git requires tags to be unique.  Each tag must refer to exactly one
check-in.  Fossil does not have this restriction, and so it is common
in Fossil to tag every release check-in of a project with the "release"
tag, so that all historical releases can be found all at once.
([example](/timeline?t=release))

Git does not allow this.  In Git, the "release" tag must refer to just one
check-in.  The work-around is that the non-unique tag in the Git export is 
made to refer to only the most recent check-in with that tag.

## (5) Amendments To Check-ins

Both Fossil and Git are based on blockchain and hence check-ins are
immutable. However, Fossil has a mechanism by which tags can be added
the blockchain to provide after-the-fact corrections to prior check-ins.

For example, tags can be added to check-ins that correct typos in the
check-in comment.  The original check-in is unchanged and the
original comment is preserved in the blockchain.  But
software that displays the check-ins knows to look for the comment-change
tag and if present displays the corrected comment rather than the original.
([Example](/info/8ed91bbe44d0d383) changing the typo "os" into "so".)

Git has no such mechanism for providing corrections or clarifications to
historical check-ins.

When exporting from Fossil to Git, the latest corrections to a Fossil check-in
are used to generate the corresponding Git check-in.  But once the Git
check-in has been created, any subsequent corrections are omitted as there
is no way to transfer those corrections to Git.
