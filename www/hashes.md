# Hashes: Fossil Artifact Identification

All artifacts in Fossil are identified by a unique hash, currently using
[the SHA3 algorithm by default][hpol], but historically using the SHA1
algorithm:

| Algorithm | Raw Bits | Hexadecimal digits |
|-----------|----------|--------------------|
| SHA3-256  | 256      | 64                 |
| SHA1      | 160      | 40                 |

There are many types of artifacts in Fossil: commits (a.k.a. check-ins),
tickets, ticket comments, wiki articles, forum postings, file data
belonging to check-ins, etc. ([More info...](./concepts.wiki#artifacts)).

There is a loose hierarchy of terms used instead of “hash” in various
parts of the Fossil UI, which we cover in the sections below.


## Names

Several Fossil interfaces accept [a wide variety of check-in
names][cin]: commit artifact hashes, ISO8601 date strings, branch names,
etc. Fossil interfaces that accept any of these options usually
document the parameter as “NAME”, so we will use that form to refer to
this specialized use.

Artifact hashes are only one of many different types of NAME.  We use
the broad term “NAME” to refer to the whole class of options. We use
more specific terms when we mean one particular type of NAME.


## Versions

When an artifact hash refers to a specific commit, Fossil sometimes
calls it a “VERSION,” a “commit ID,” or a “check-in ID.”
We may eventually settle on one of these terms, but all three are
currently in common use within Fossil’s docs, UI, and programming
interfaces.

A VERSION is a specific type of artifact hash, distinct
from, let us say, a wiki article artifact hash.

A unique prefix of a VERSION hash is itself a VERSION. That is, if your
repository has exactly one commit artifact with a hash prefix of
“abc123”, then that is a valid version string as long as it remains
unambiguous.



## <a id="uvh"></a>UUIDs

Fossil uses the term “UUID” as a short alias for “artifact hash” in its
internals. There are a few places where this leaks out into external
interfaces, which we cover in the sections below. Going forward, we
prefer one of the terms above in public interfaces instead.

Whether this short alias is correct is debateable.

One argument is that since "UUID" is an acronym for “Universally Unique
Identifier,” and both SHA1 and SHA3-256 are larger and stronger than the
128-bit algorithms used by “proper” UUIDs, Fossil artifact hashes are
*more universally unique*. It is therefore quibbling to say that Fossil
UUIDs are not actually UUIDs. One wag suggested that Fossil artifact
hashes be called MUIDs: multiversally unique IDs.

The common counterargument is that the acronym “UUID” was created for [a
particular type of universally-unique ID][uuid], with particular ASCII
and bitfield formats, and with particular meaning given to certain of
its bits. In that sense, no Fossil “UUID” can be used as a proper UUID.

Be warned: attempting to advance the second position on the Fossil
discussion forum will get you nowhere at this late date. We’ve had the
debates, we’ve done the engineering, and we’ve made our evaluation. It’s
a settled matter: internally within Fossil, “UUID” is defined as in this
section’s leading paragraph.

To those who remain unconvinced, “fixing” this would require touching
almost every source code file in Fossil in a total of about a thousand
separate locations. (Not exaggeration, actual data.) This would be a
massive undertaking simply to deal with a small matter of terminology,
with a high risk of creating bugs and downstream incompatibilities.
Therefore, we are highly unlikely to change this ourselves, and we are
also unlikely to accept a patch that attempts to fix it.


### Repository DB Schema

The primary place where you find "UUID" in Fossil is in the `blob.uuid`
table column, in code dealing with that column, and in code manipulating
*other* data that *refers* to that column. This is a key lookup column
in the most important Fossil DB table, so it influences broad swaths of
the Fossil internals.

For example, C code that refers to SQL result data on `blob.uuid`
usually calls the variable `zUuid`. That value may then be inserted into
a table like `ticket.tkt_uuid`, creating a reference back to
`blob.uuid`, and then be passed to a function like `uuid_to_rid()`.
There is no point renaming a single one of these in isolation: it would
create needless terminology conflicts, making the code hard to read and
understand, risking the creation of new bugs.

You may have local SQL code that digs into the repository DB using these
column names. While you may rest easy, assured now that we are highly
unlikely to ever rename these columns, the Fossil repository DB schema
is not considered an external user interface, and internal interfaces
are subject to change at any time. We suggest switching to a more stable
API: [the JSON API][japi], [`timeline.rss`][trss], [TH1][th1], etc.


### TH1 Scripting Interfaces

Some [TH1][th1] interfaces expose Fossil internals flowing from
`blob.uuid`, so “UUID” is a short alias for “artifact hash” in TH1.  For
example, the `$tkt_uuid` variable &mdash; available when [customizing
the ticket system][ctkt] &mdash; is a ticket artifact hash, exposing the
`ticket.tkt_uuid` column, which has a SQL relation to `blob.uuid`.

TH1 is a longstanding public programming interface. We cannot rename its
interfaces without breaking existing TH1 Fossil customizations. We are
also unlikely to provide a parallel set of variables with “better”
names, since that would create a mismatch with respect to the internals
they expose, creating a different sort of developer confusion in its
place.


### JSON API Parameters and Outputs

[The JSON API][japi] frequently uses the term “UUID” in the same sort of way,
most commonly in [artifact][jart] and [timeline][jtim] APIs. As with
TH1, we can’t change this without breaking code that uses the JSON
API as originally designed, so we take the same stance.


### `manifest.uuid`

If you have [the `manifest` setting][mset] enabled, Fossil writes a file
called `manifest.uuid` at the root of the check-out tree containing the
commit hash for the current checked-out version. Because this is a
public interface that existing code depends on, we are unwilling to
rename the file.


[cin]:  ./checkin_names.wiki
[ctkt]: ./custom_ticket.wiki
[hpol]: ./hashpolicy.wiki
[japi]: ./json-api/
[jart]: ./json-api/api-artifact.md
[jtim]: ./json-api/api-timeline.md
[mset]: /help?cmd=manifest
[th1]:  ./th1.md
[trss]: /help?cmd=/timeline.rss
[tvb]:  ./branching.wiki
[uuid]: https://en.wikipedia.org/wiki/Universally_unique_identifier
