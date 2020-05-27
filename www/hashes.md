# Hashes: Fossil Artifact Identification

All artifacts in Fossil are identified by a unique hash, currently using
[the SHA3 algorithm by default][hpol], but historically using the SHA1
algorithm. Therefore, there are two full-length hash formats used by
Fossil:

| Algorithm | Raw Bits | Hex ASCII Bytes |
|-----------|----------|-----------------|
| SHA3-256  | 256      | 64              |
| SHA1      | 160      | 40              |

There are many types of artifacts in Fossil: commits (a.k.a. check-ins),
tickets, ticket comments, wiki articles, forum postings, file data
belonging to check-ins, etc. ([More info...](./concepts.wiki#artifacts)).

There is a loose hierarchy of terms used instead of “hash” in various
parts of the Fossil UI, terms we try to use consistently, though we have
not always succeeded. We cover each of those terms in the sections
below.


## Names

Several Fossil interfaces accept [a wide variety of check-in
names][cin]: commit artifact hashes, ISO8601 date strings, branch names,
etc.

Artifact hashes are names, but not all names are artifact hashes. We use
the broader term to refer to the whole class of options, and we use the
specific terms when we mean one particular type of name.


## Versions

When an artifact hash refers to a specific commit, Fossil sometimes
calls it a “VERSION,” a “commit ID,” or a “check-in ID.” This is a
specific type of artifact hash, distinct from, let us say, a wiki
article artifact hash.

We may eventually settle on one of these terms, but all three are
currently in common use within Fossil’s docs, UI, and programming
interfaces.

A unique prefix of a VERSION hash is itself a VERSION. That is, if your
repository has exactly one commit artifact with a hash prefix of
“abc123”, then that is a valid version string as long as it remains
unambiguous.



## <a id="uvh"></a>UUIDs: An Unfortunate Historical Artifact

Historically, Fossil incorrectly used the term “[UUID][uuid]” where it
should use the term “artifact hash” instead. There are two primary
problems with miscalling Fossil artifact hashes UUIDs:

1. UUIDs are always 128 bits in length — 32 hex ASCII bytes — making
   them shorter than any actual Fossil artifact hash.

2. Artifact hashes are necessarily highly pseudorandom blobs, but only
   [version 4 UUIDs][v4] are pseudorandom in the same way. Other UUID
   types have non-random meanings for certain subgroups of the bits,
   restrictions that Fossil artifact hashes do not meet.

Therefore, no Fossil hash can ever be a proper UUID.

Nevertheless, there are several places in Fossil where we still use the
term UUID, primarily for backwards compatibility:


### Repository DB Schema

Almost all of these uses flow from the `blob.uuid` table column. This is
a key lookup column in the most important persistent Fossil DB table, so
it influences broad swaths of the Fossil internals.

Someday we may rename this column and those it has influenced (e.g.
`purgeitem.uuid`, `shun.uuid`, and `ticket.tkt_uuid`) by making Fossil
detect the outdated schema and silently upgrade it, coincident with
updating all of the SQL in Fossil that refers to these columns. Until
then, Fossil will continue to have “UUID” all through its internals.

In order to avoid needless terminology conflicts, Fossil code that
refers to these misnamed columns also uses some variant of “UUID.” For
example, C code that refers to SQL result data on `blob.uuid` usually
calls the variable `zUuid`. Another example is the internal function
`uuid_to_rid()`. Until and unless we decide to rename these DB columns,
we will keep these associated internal identifiers unchanged.

You may have local SQL code that digs into the repository DB using these
column names. If so, be warned: we are not inclined to consider
existence of such code sufficient reason to avoid renaming the columns.
The Fossil repository DB schema is not considered an external user
interface, and internal interfaces are subject to change at any time. We
suggest switching to a more stable API: the JSON API, `/timeline.rss`,
TH1, etc.

There are also some temporary tables that misuse “UUID” in this way.
(`description.uuid`, `timeline.uuid`, `xmark.uuid`, etc.) There’s a good
chance we’ll fix these before we fix the on-disk DB schema since no
other code can depend on them.


### TH1 Scripting Interfaces

Some [TH1](./th1.md) interfaces use “UUID” where they actually mean some
kind of hash. For example, the `$tkt_uuid` variable, available via TH1
when [customizing Fossil’s ticket system][ctkt].

Because this is considered a public programming interface, we are
unwilling to unilaterally rename such TH1 variables, even though they
are “wrong.” For now, we are simply documenting the misuse. Later, we
may provide a parallel interface — e.g. `$tkt_hash` in this case — and
drop mention of the old interface from the documentation, but still
support it.


### JSON API Parameters and Outputs

The JSON API frequently misuses the term “UUID” in the same sort of way,
most commonly in [artifact][jart] and [timeline][jtim] APIs. As with the
prior case, we can’t fix these without breaking code that uses the JSON
API as originally designed, so our solutions are the same: document the
misuse here for now, then possibly provide a backwards-compatible fix
later.


### `manifest.uuid`

If you have [the `manifest` setting][mset] enabled, Fossil writes a file
called `manifest.uuid` at the root of the check-out tree containing the
commit hash for the current checked-out version. Because this is a
public interface, we are unwilling to rename the file for correctness.


[cin]:  ./checkin_names.wiki
[ctkt]: ./custom_ticket.wiki
[hpol]: ./hashpolicy.wiki
[jart]: ./json-api/api-artifact.md
[jtim]: ./json-api/api-timeline.md
[mset]: /help?cmd=manifest
[tvb]:  ./branching.wiki
[uuid]: https://en.wikipedia.org/wiki/Universally_unique_identifier
[v4]:   https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)
